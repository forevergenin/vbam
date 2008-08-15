// VisualBoyAdvance - Nintendo Gameboy/GameboyAdvance (TM) emulator.
// Copyright (C) 1999-2003 Forgotten
// Copyright (C) 2004 Forgotten and the VBA development team
// Copyright (C) 2004-2006 VBA development team
// Copyright (C) 2007 Shay Green (blargg)

// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2, or(at your option)
// any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software Foundation,
// Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

#include <memory.h>

#include "Sound.h"

#include "agb/GBA.h"
#include "Globals.h"
#include "Util.h"
#include "Port.h"

#include "dmg/gb_apu/Gb_Apu.h"
#include "dmg/gb_apu/Multi_Buffer.h"

extern bool stopState;      // TODO: silence sound when true

int soundQuality       = 2;
int soundInterpolation = 0;
float soundFiltering   = 1;
static float soundFiltering_;
int soundVolume        = 0;
static int soundVolume_;
bool soundEcho         = false;
int soundEnableFlag    = 0x3ff; // emulator channels enabled

int const SOUND_CLOCK_TICKS_ = 167772;
int SOUND_CLOCK_TICKS = SOUND_CLOCK_TICKS_;
int soundTicks = SOUND_CLOCK_TICKS_;

u16 soundFinalWave [1470];
int soundBufferLen = sizeof soundFinalWave;

int soundDebug        = 0;
u32 soundNextPosition = 0;
bool soundOffFlag     = false;
bool soundPaused      = true;

bool soundLowPass = false;
bool soundReverse = false;

void interp_rate() { /* empty for now */ }

class Gba_Pcm {
public:
	void init();
	void apply_control( int idx );
	void update( int dac );
	void end_frame( blip_time_t );

private:
	Blip_Buffer* output;
	blip_time_t last_time;
	int last_amp;
	int shift;
};

class Gba_Pcm_Fifo {
public:
	int     which;
	Gba_Pcm pcm;

	void write_control( int data );
	void write_fifo( int data );
	void timer_overflowed( int which_timer );

	// public only so save state routines can access it
	int  readIndex;
	int  count;
	int  writeIndex;
	u8   fifo [32];
	int  dac;
private:

	int  timer;
	bool enabled;
};

static Gba_Pcm_Fifo     pcm [2];
static Gb_Apu*          gb_apu;
static Stereo_Buffer*   stereo_buffer;

static Blip_Synth<blip_best_quality,1> pcm_synth [3]; // 32 kHz, 16 kHz, 8 kHz

static inline blip_time_t blip_time()
{
	return SOUND_CLOCK_TICKS - soundTicks;
}

void Gba_Pcm::init()
{
	output    = 0;
	last_time = 0;
	last_amp  = 0;
	shift     = 0;
}

void Gba_Pcm::apply_control( int idx )
{
	shift = ~ioMem [SGCNT0_H] >> (2 + idx) & 1;

	int ch = 0;
	if ( (soundEnableFlag >> idx & 0x100) && (ioMem [NR52] & 0x80) )
		ch = ioMem [SGCNT0_H+1] >> (idx * 4) & 3;

	Blip_Buffer* out = 0;
	switch ( ch )
	{
	case 1: out = stereo_buffer->right();  break;
	case 2: out = stereo_buffer->left();   break;
	case 3: out = stereo_buffer->center(); break;
	}

	if ( output != out )
	{
		if ( output )
		{
			output->set_modified();
			pcm_synth [0].offset( blip_time(), -last_amp, output );
		}
		last_amp = 0;
		output = out;
	}
}

void Gba_Pcm::end_frame( blip_time_t time )
{
	last_time -= time;
	if ( last_time < -2048 )
		last_time = -2048;

	if ( output )
		output->set_modified();
}

void Gba_Pcm::update( int dac )
{
	if ( output )
	{
		blip_time_t time = blip_time();

		dac = (s8) dac >> shift;
		int delta = dac - last_amp;
		if ( delta )
		{
			last_amp = dac;

			int filter = 0;
			if ( soundInterpolation )
			{
				// base filtering on how long since last sample was output
				int period = time - last_time;

				int idx = (unsigned) period / 512;
				if ( idx >= 3 )
					idx = 3;

				static int const filters [4] = { 0, 0, 1, 2 };
				filter = filters [idx];
			}

			pcm_synth [filter].offset( time, delta, output );
		}
		last_time = time;
	}
}

void Gba_Pcm_Fifo::timer_overflowed( int which_timer )
{
	if ( which_timer == timer && enabled )
	{
		if ( count <= 16 )
		{
			// Need to fill FIFO
			CPUCheckDMA( 3, which ? 4 : 2 );
			if ( count <= 16 )
			{
				// Not filled by DMA, so fill with 16 bytes of silence
				int reg = which ? FIFOB_L : FIFOA_L;
				for ( int n = 4; n--; )
				{
					soundEvent(reg  , (u16)0);
					soundEvent(reg+2, (u16)0);
				}
			}
		}

		// Read next sample from FIFO
		count--;
		dac = fifo [readIndex];
		readIndex = (readIndex + 1) & 31;
		pcm.update( dac );
	}
}

void Gba_Pcm_Fifo::write_control( int data )
{
	enabled = (data & 0x0300) ? true : false;
	timer   = (data & 0x0400) ? 1 : 0;

	if ( data & 0x0800 )
	{
		// Reset
		writeIndex = 0;
		readIndex  = 0;
		count      = 0;
		dac        = 0;
		memset( fifo, 0, sizeof fifo );
	}

	pcm.apply_control( which );
	pcm.update( dac );
}

void Gba_Pcm_Fifo::write_fifo( int data )
{
	fifo [writeIndex  ] = data & 0xFF;
	fifo [writeIndex+1] = data >> 8;
	count += 2;
	writeIndex = (writeIndex + 2) & 31;
}

static void apply_control()
{
	pcm [0].pcm.apply_control( 0 );
	pcm [1].pcm.apply_control( 1 );
}

static int gba_to_gb_sound( int addr )
{
	static const int table [0x40] =
	{
		0xFF10,     0,0xFF11,0xFF12,0xFF13,0xFF14,     0,     0,
		0xFF16,0xFF17,     0,     0,0xFF18,0xFF19,     0,     0,
		0xFF1A,     0,0xFF1B,0xFF1C,0xFF1D,0xFF1E,     0,     0,
		0xFF20,0xFF21,     0,     0,0xFF22,0xFF23,     0,     0,
		0xFF24,0xFF25,     0,     0,0xFF26,     0,     0,     0,
		     0,     0,     0,     0,     0,     0,     0,     0,
		0xFF30,0xFF31,0xFF32,0xFF33,0xFF34,0xFF35,0xFF36,0xFF37,
		0xFF38,0xFF39,0xFF3A,0xFF3B,0xFF3C,0xFF3D,0xFF3E,0xFF3F,
	};
	if ( addr >= 0x60 && addr < 0xA0 )
		return table [addr - 0x60];
	return 0;
}

void soundEvent(u32 address, u8 data)
{
	int gb_addr = gba_to_gb_sound( address );
	if ( gb_addr )
	{
		ioMem[address] = data;
		gb_apu->write_register( blip_time(), gb_addr, data );

		if ( address == NR52 )
			apply_control();
	}

	// TODO: what about byte writes to SGCNT0_H etc.?
}

static void apply_volume( bool apu_only = false )
{
	if ( !apu_only )
		soundVolume_ = soundVolume;

	// Emulator volume
	static float const vols [6] = { 1, 2, 3, 4, 0.25, 0.5 };
	double const volume = vols [soundVolume_];

	if ( gb_apu )
	{
		static float const apu_vols [4] = { 0.25, 0.5, 1, 0.25 };
		gb_apu->volume( volume * apu_vols [ioMem [SGCNT0_H] & 3] );
	}

	if ( !apu_only )
	{
		for ( int i = 0; i < 3; i++ )
			pcm_synth [i].volume( 0.66 / 256 * volume );
	}
}

static void write_SGCNT0_H( int data )
{
	WRITE16LE( &ioMem [SGCNT0_H], data & 0x770F );
	pcm [0].write_control( data      );
	pcm [1].write_control( data >> 4 );
	apply_volume( true );
}

void soundEvent(u32 address, u16 data)
{
	switch ( address )
	{
	case SGCNT0_H:
		write_SGCNT0_H( data );
		break;

	case FIFOA_L:
	case FIFOA_H:
		pcm [0].write_fifo( data );
		WRITE16LE( &ioMem[address], data );
		break;

	case FIFOB_L:
	case FIFOB_H:
		pcm [1].write_fifo( data );
		WRITE16LE( &ioMem[address], data );
		break;

	case 0x88:
		data &= 0xC3FF;
		WRITE16LE( &ioMem[address], data );
		break;

	default:
		soundEvent( address & ~1, (u8) (data     ) ); // even
		soundEvent( address |  1, (u8) (data >> 8) ); // odd
		break;
	}
}

void soundTimerOverflow(int timer)
{
	pcm [0].timer_overflowed( timer );
	pcm [1].timer_overflowed( timer );
}

static void end_frame( blip_time_t time )
{
	pcm [0].pcm.end_frame( time );
	pcm [1].pcm.end_frame( time );

	gb_apu       ->end_frame( time );
	stereo_buffer->end_frame( time );
}

static void flush_samples()
{
	// soundBufferLen should have a whole number of sample pairs
	assert( soundBufferLen % (2 * sizeof *soundFinalWave) == 0 );

	// number of samples in output buffer
	int const out_buf_size = soundBufferLen / sizeof *soundFinalWave;

	// Keep filling and writing soundFinalWave until it can't be fully filled
	while ( stereo_buffer->samples_avail() >= out_buf_size )
	{
		stereo_buffer->read_samples( (blip_sample_t*) soundFinalWave, out_buf_size );
		if(systemSoundOn)
		{
			if(soundPaused)
				soundResume();

			systemWriteDataToSoundBuffer();
		}
	}
}

static void apply_filtering()
{
	soundFiltering_ = soundFiltering;

	int const base_freq = (int) (32768 - soundFiltering_ * 16384);
	int const nyquist = stereo_buffer->sample_rate() / 2;

	for ( int i = 0; i < 3; i++ )
	{
		int cutoff = base_freq >> i;
		if ( cutoff > nyquist )
			cutoff = nyquist;
		pcm_synth [i].treble_eq( blip_eq_t( 0, 0, stereo_buffer->sample_rate(), cutoff ) );
	}
}

static void soundTick()
{
 	if ( systemSoundOn && gb_apu && stereo_buffer )
	{
		// Run sound hardware to present
		end_frame( SOUND_CLOCK_TICKS );

		flush_samples();

		if ( soundFiltering_ != soundFiltering )
			apply_filtering();

		if ( soundVolume_ != soundVolume )
			apply_volume();
	}
}

void (*psoundTickfn)() = soundTick;

static void apply_muting()
{
	if ( !stereo_buffer || !ioMem )
		return;

	// PCM
	apply_control();

	if ( gb_apu )
	{
		// APU
		for ( int i = 0; i < 4; i++ )
		{
			if ( soundEnableFlag >> i & 1 )
				gb_apu->set_output( stereo_buffer->center(),
						stereo_buffer->left(), stereo_buffer->right(), i );
			else
				gb_apu->set_output( 0, 0, 0, i );
		}
	}
}

static void reset_apu()
{
	gb_apu->reset( gb_apu->mode_agb, true );

	if ( stereo_buffer )
		stereo_buffer->clear();

	soundTicks = SOUND_CLOCK_TICKS;
}

static void remake_stereo_buffer()
{
	if ( !ioMem )
		return;

	// Clears pointers kept to old stereo_buffer
	pcm [0].pcm.init();
	pcm [1].pcm.init();

	// Stereo_Buffer
	delete stereo_buffer;
	stereo_buffer = 0;

	stereo_buffer = new Stereo_Buffer; // TODO: handle out of memory
	long const sample_rate = 44100 / soundQuality;
	stereo_buffer->set_sample_rate( sample_rate ); // TODO: handle out of memory
	stereo_buffer->clock_rate( gb_apu->clock_rate );

	// PCM
	pcm [0].which = 0;
	pcm [1].which = 1;
	apply_filtering();

	// APU
	if ( !gb_apu )
	{
		gb_apu = new Gb_Apu; // TODO: handle out of memory
		reset_apu();
	}

	apply_muting();
	apply_volume();
}

void setsystemSoundOn(bool value)
{
	systemSoundOn = value;
}

void setsoundPaused(bool value)
{
	soundPaused = value;
}

void soundShutdown()
{
	systemSoundShutdown();
}

void soundPause()
{
	systemSoundPause();
	setsoundPaused(true);
}

void soundResume()
{
	systemSoundResume();
	setsoundPaused(false);
}

void soundEnable(int channels)
{
	soundEnableFlag = channels;
	apply_muting();
}

void soundDisable(int channels)
{
	soundEnableFlag &= ~channels;
	apply_muting();
}

int soundGetEnable()
{
	return (soundEnableFlag & 0x30f);
}

void soundReset()
{
	systemSoundReset();

	remake_stereo_buffer();
	reset_apu();

	setsoundPaused(true);
	SOUND_CLOCK_TICKS = SOUND_CLOCK_TICKS_;
	soundTicks        = SOUND_CLOCK_TICKS_;

	soundNextPosition = 0;

	soundEvent( NR52, (u8) 0x80 );
}

bool soundInit()
{
	if ( !systemSoundInit() )
		return false;

	soundPaused = true;
	return true;
}

void soundSetQuality(int quality)
{
	if ( soundQuality != quality )
	{
		if ( systemCanChangeSoundQuality() )
		{
			if ( !soundOffFlag )
				soundShutdown();

			soundQuality      = quality;
			soundNextPosition = 0;

			if ( !soundOffFlag )
				soundInit();
		}
		else
		{
			soundQuality      = quality;
			soundNextPosition = 0;
		}

		remake_stereo_buffer();
	}
}

static int dummy_state [16];

#define SKIP( type, name ) { dummy_state, sizeof (type) }

#define LOAD( type, name ) { &name, sizeof (type) }

static struct {
	gb_apu_state_t apu;

	// old state
	u8 soundDSAValue;
	int soundDSBValue;
} state;

// Old GBA sound state format
static variable_desc old_gba_state [] =
{
	SKIP( int, soundPaused ),
	SKIP( int, soundPlay ),
	SKIP( int, soundTicks ),
	SKIP( int, SOUND_CLOCK_TICKS ),
	SKIP( int, soundLevel1 ),
	SKIP( int, soundLevel2 ),
	SKIP( int, soundBalance ),
	SKIP( int, soundMasterOn ),
	SKIP( int, soundIndex ),
	SKIP( int, sound1On ),
	SKIP( int, sound1ATL ),
	SKIP( int, sound1Skip ),
	SKIP( int, sound1Index ),
	SKIP( int, sound1Continue ),
	SKIP( int, sound1EnvelopeVolume ),
	SKIP( int, sound1EnvelopeATL ),
	SKIP( int, sound1EnvelopeATLReload ),
	SKIP( int, sound1EnvelopeUpDown ),
	SKIP( int, sound1SweepATL ),
	SKIP( int, sound1SweepATLReload ),
	SKIP( int, sound1SweepSteps ),
	SKIP( int, sound1SweepUpDown ),
	SKIP( int, sound1SweepStep ),
	SKIP( int, sound2On ),
	SKIP( int, sound2ATL ),
	SKIP( int, sound2Skip ),
	SKIP( int, sound2Index ),
	SKIP( int, sound2Continue ),
	SKIP( int, sound2EnvelopeVolume ),
	SKIP( int, sound2EnvelopeATL ),
	SKIP( int, sound2EnvelopeATLReload ),
	SKIP( int, sound2EnvelopeUpDown ),
	SKIP( int, sound3On ),
	SKIP( int, sound3ATL ),
	SKIP( int, sound3Skip ),
	SKIP( int, sound3Index ),
	SKIP( int, sound3Continue ),
	SKIP( int, sound3OutputLevel ),
	SKIP( int, sound4On ),
	SKIP( int, sound4ATL ),
	SKIP( int, sound4Skip ),
	SKIP( int, sound4Index ),
	SKIP( int, sound4Clock ),
	SKIP( int, sound4ShiftRight ),
	SKIP( int, sound4ShiftSkip ),
	SKIP( int, sound4ShiftIndex ),
	SKIP( int, sound4NSteps ),
	SKIP( int, sound4CountDown ),
	SKIP( int, sound4Continue ),
	SKIP( int, sound4EnvelopeVolume ),
	SKIP( int, sound4EnvelopeATL ),
	SKIP( int, sound4EnvelopeATLReload ),
	SKIP( int, sound4EnvelopeUpDown ),
	LOAD( int, soundEnableFlag ),
	SKIP( int, soundControl ),
	LOAD( int, pcm [0].readIndex ),
	LOAD( int, pcm [0].count ),
	LOAD( int, pcm [0].writeIndex ),
	SKIP( u8,  soundDSAEnabled ), // was bool, which was one byte on MS compiler
	SKIP( int, soundDSATimer ),
	LOAD( u8 [32], pcm [0].fifo ),
	LOAD( u8,  state.soundDSAValue ),
	LOAD( int, pcm [1].readIndex ),
	LOAD( int, pcm [1].count ),
	LOAD( int, pcm [1].writeIndex ),
	SKIP( int, soundDSBEnabled ),
	SKIP( int, soundDSBTimer ),
	LOAD( u8 [32], pcm [1].fifo ),
	LOAD( int, state.soundDSBValue ),

	// skipped manually
	//LOAD( int, soundBuffer[0][0], 6*735 },
	//LOAD( int, soundFinalWave[0], 2*735 },
	{ NULL, 0 }
};

variable_desc old_gba_state2 [] =
{
	LOAD( u8 [0x20], state.apu.regs [0x20] ),
	SKIP( int, sound3Bank ),
	SKIP( int, sound3DataSize ),
	SKIP( int, sound3ForcedOutput ),
	{ NULL, 0 }
};

// New state format
static variable_desc gba_state [] =
{
	// PCM
	LOAD( int, pcm [0].readIndex ),
	LOAD( int, pcm [0].count ),
	LOAD( int, pcm [0].writeIndex ),
	LOAD(u8[32],pcm[0].fifo ),
	LOAD( int, pcm [0].dac ),

	SKIP( int [4], room_for_expansion ),

	LOAD( int, pcm [1].readIndex ),
	LOAD( int, pcm [1].count ),
	LOAD( int, pcm [1].writeIndex ),
	LOAD(u8[32],pcm[1].fifo ),
	LOAD( int, pcm [1].dac ),

	SKIP( int [4], room_for_expansion ),

	// APU
	LOAD( u8 [0x40], state.apu.regs ),      // last values written to registers and wave RAM (both banks)
	LOAD( int, state.apu.frame_time ),      // clocks until next frame sequencer action
	LOAD( int, state.apu.frame_phase ),     // next step frame sequencer will run

	LOAD( int, state.apu.sweep_freq ),      // sweep's internal frequency register
	LOAD( int, state.apu.sweep_delay ),     // clocks until next sweep action
	LOAD( int, state.apu.sweep_enabled ),
	LOAD( int, state.apu.sweep_neg ),       // obscure internal flag
	LOAD( int, state.apu.noise_divider ),
	LOAD( int, state.apu.wave_buf ),        // last read byte of wave RAM

	LOAD( int [4], state.apu.delay ),       // clocks until next channel action
	LOAD( int [4], state.apu.length_ctr ),
	LOAD( int [4], state.apu.phase ),       // square/wave phase, noise LFSR
	LOAD( int [4], state.apu.enabled ),     // internal enabled flag

	LOAD( int [3], state.apu.env_delay ),   // clocks until next envelope action
	LOAD( int [3], state.apu.env_volume ),
	LOAD( int [3], state.apu.env_enabled ),

	SKIP( int [13], room_for_expansion ),

	// Emulator
	LOAD( int, soundEnableFlag ),

	SKIP( int [15], room_for_expansion ),

	{ NULL, 0 }
};

// Reads and discards count bytes from in
static void skip_read( gzFile in, int count )
{
	char buf [512];

	while ( count )
	{
		int n = sizeof buf;
		if ( n > count )
			n = count;

		count -= n;
		utilGzRead( in, buf, n );
	}
}

void soundSaveGame( gzFile out )
{
	gb_apu->save_state( &state.apu );

	// Be sure areas for expansion get written as zero
	memset( dummy_state, 0, sizeof dummy_state );

	utilWriteData( out, gba_state );
}

static void soundReadGameOld( gzFile in, int version )
{
	// Read main data
	utilReadData( in, old_gba_state );
	skip_read( in, 6*735 + 2*735 );

	// Copy APU regs
	static int const regs_to_copy [] = {
		NR10, NR11, NR12, NR13, NR14,
		      NR21, NR22, NR23, NR24,
		NR30, NR31, NR32, NR33, NR34,
		      NR41, NR42, NR43, NR44,
		NR50, NR51, NR52, -1
	};

	ioMem [NR52] |= 0x80; // old sound played even when this wasn't set (power on)

	for ( int i = 0; regs_to_copy [i] >= 0; i++ )
		state.apu.regs [gba_to_gb_sound( regs_to_copy [i] ) - 0xFF10] = ioMem [regs_to_copy [i]];

	// Copy wave RAM to both banks
	memcpy( &state.apu.regs [0x20], &ioMem [0x90], 0x10 );
	memcpy( &state.apu.regs [0x30], &ioMem [0x90], 0x10 );

	// Read both banks of wave RAM if available
	if ( version >= SAVE_GAME_VERSION_3 )
		utilReadData( in, old_gba_state2 );

	// Restore PCM
	pcm [0].dac = state.soundDSAValue;
	pcm [1].dac = state.soundDSBValue;

	int quality = utilReadInt( in ); // ignore this crap
}

#include <stdio.h>

void soundReadGame( gzFile in, int version )
{
	// Prepare APU and default state
	reset_apu();
	gb_apu->save_state( &state.apu );

	if ( version > SAVE_GAME_VERSION_9 )
		utilReadData( in, gba_state );
	else
		soundReadGameOld( in, version );

	gb_apu->load_state( state.apu );
	write_SGCNT0_H( READ16LE( &ioMem [SGCNT0_H] ) & 0x770F );

	apply_muting();
}
