// license: BSD-3-Clause
// copyright-holders: Dirk Best
/***************************************************************************

    Informer 213 (IBM 374/SNA V.22)
    Informer 213 AE (VT-100)

    Hardware:
    - EF68B09EP
    - 2x TC5564PL-15 [8k] (next to CG ROM)
    - 1x TC5565APL-15 [8k] + 2x TMS4464-15NL [32k] (next to CPU)
    - Z0853006PSC SCC
    - ASIC (INFORMER 44223)
    - 18.432 MHz XTAL

    TODO:
    - Figure out the ASIC and how it's connected

***************************************************************************/

#include "emu.h"
#include "cpu/m6809/m6809.h"
#include "machine/z80scc.h"
#include "machine/informer_213_kbd.h"
#include "sound/beep.h"
#include "emupal.h"
#include "screen.h"
#include "speaker.h"


//**************************************************************************
//  TYPE DEFINITIONS
//**************************************************************************

class informer_213_state : public driver_device
{
public:
	informer_213_state(const machine_config &mconfig, device_type type, const char *tag) :
		driver_device(mconfig, type, tag),
		m_maincpu(*this, "maincpu"),
		m_screen(*this, "screen"),
		m_palette(*this, "palette"),
		m_scc(*this, "scc"),
		m_beep(*this, "beep"),
		m_vram(*this, "vram"),
		m_aram(*this, "aram"),
		m_chargen(*this, "chargen")
	{ }

	void informer_213(machine_config &config);

protected:
	void machine_start() override;
	void machine_reset() override;

private:
	required_device<cpu_device> m_maincpu;
	required_device<screen_device> m_screen;
	required_device<palette_device> m_palette;
	required_device<scc8530_device> m_scc;
	required_device<beep_device> m_beep;
	required_shared_ptr<uint8_t> m_vram;
	required_shared_ptr<uint8_t> m_aram;
	required_region_ptr<uint8_t> m_chargen;

	void mem_map(address_map &map);

	uint32_t screen_update(screen_device &screen, bitmap_rgb32 &bitmap, const rectangle &cliprect);
	void vblank_w(int state);
	void vram_start_addr_w(offs_t offset, uint8_t data);
	void vram_end_addr_w(offs_t offset, uint8_t data);
	void vram_start_addr2_w(offs_t offset, uint8_t data);
	void cursor_addr_w(offs_t offset, uint8_t data);
	void cursor_start_w(uint8_t data);
	void cursor_end_w(uint8_t data);
	void screen_ctrl_w(uint8_t data);

	uint8_t unk_42_r();
	void unk_42_w(uint8_t data);

	void bell_w(uint8_t data);

	void kbd_int_w(int state);
	uint8_t vector_r();

	uint16_t m_vram_start_addr;
	uint16_t m_vram_end_addr;
	uint16_t m_vram_start_addr2;
	uint8_t m_cursor_start;
	uint8_t m_cursor_end;
	uint16_t m_cursor_addr;
	uint8_t m_screen_ctrl;
	uint8_t m_unk_42;
	uint8_t m_vector;
};


//**************************************************************************
//  ADDRESS MAPS
//**************************************************************************

void informer_213_state::mem_map(address_map &map)
{
	map(0x0000, 0x0000).rw("kbd", FUNC(informer_213_kbd_hle_device::read), FUNC(informer_213_kbd_hle_device::write));
	map(0x0006, 0x0007).unmapr().w(FUNC(informer_213_state::vram_start_addr_w));
	map(0x0008, 0x0009).unmapr().w(FUNC(informer_213_state::vram_end_addr_w));
	map(0x000a, 0x000b).unmapr().w(FUNC(informer_213_state::vram_start_addr2_w));
	map(0x000d, 0x000d).unmapr().w(FUNC(informer_213_state::cursor_start_w));
	map(0x000e, 0x000e).unmapr().w(FUNC(informer_213_state::cursor_end_w));
	map(0x000f, 0x0010).unmapr().w(FUNC(informer_213_state::cursor_addr_w));
	map(0x0021, 0x0021).w(FUNC(informer_213_state::screen_ctrl_w));
	map(0x0042, 0x0042).rw(FUNC(informer_213_state::unk_42_r), FUNC(informer_213_state::unk_42_w));
	map(0x0060, 0x0060).w(FUNC(informer_213_state::bell_w));
	map(0x0100, 0x1fff).ram();
	map(0x2000, 0x3fff).ram();
	map(0x4000, 0x5fff).ram();
	map(0x6000, 0x6fff).ram().share("vram");
	map(0x7000, 0x7fff).ram().share("aram");
	map(0x8000, 0xffff).rom().region("maincpu", 0);
	map(0xfff7, 0xfff7).r(FUNC(informer_213_state::vector_r));
}


//**************************************************************************
//  INPUT PORT DEFINITIONS
//**************************************************************************

static INPUT_PORTS_START( informer_213 )
INPUT_PORTS_END


//**************************************************************************
//  VIDEO EMULATION
//**************************************************************************

uint32_t informer_213_state::screen_update(screen_device &screen, bitmap_rgb32 &bitmap, const rectangle &cliprect)
{
	offs_t chargen_base = (m_chargen.bytes() == 0x4000) ? 0x2000 : 0;

	// line at which vram splits into two windows
	int split = 24 - ((m_vram_start_addr2 - m_vram_start_addr) / 80);

	for (int y = 0; y < 26; y++)
	{
		uint8_t line_attr = 0;

		for (int x = 0; x < 80; x++)
		{
			uint16_t addr;

			// vram is split into 3 areas: display window 1, display window 2, status bar
			if (y >= 24)
				addr = 0x6000 + (y - 24) * 80 + x;
			else if (y >= split)
				addr = m_vram_start_addr + (y - split) * 80 + x;
			else
				addr = m_vram_start_addr2 + y * 80 + x;

			uint8_t code = m_vram[addr - 0x6000];
			uint8_t attr = m_aram[addr - 0x6000];

			if (code == 0xc0 || code == 0xe8)
				line_attr = attr;

			// draw 9 lines
			for (int i = 0; i < 9; i++)
			{
				uint8_t data = m_chargen[chargen_base | ((code << 4) + i)];

				// conceal
				if (line_attr & 0x08 || attr & 0x08)
					data = 0x00;

				// reverse video
				if (line_attr & 0x10 || attr & 0x10)
					data ^= 0xff;

				// underline (not supported by terminal?)
				if (line_attr & 0x20 || attr & 0x20)
					data = +data;

				// blink (todo: timing)
				if (line_attr & 0x40 || attr & 0x40)
					data = m_screen->frame_number() & 0x20 ? 0x00 :data;

				if (code == 0xc0 || code == 0xe8)
					data = 0;

				if (BIT(m_screen_ctrl, 5))
					data ^= 0xff;

				// cursor
				if (BIT(m_cursor_start, 5) == 1 && addr == m_cursor_addr && y < 24)
					if (i >= (m_cursor_start & 0x0f) && i < (m_cursor_end & 0x0f))
						if (!(BIT(m_screen_ctrl, 4) == 0 && (m_screen->frame_number() & 0x20)))
							data = 0xff;

				// 6 pixels of the character
				bitmap.pix32(y * 9 + i, x * 6 + 0) = BIT(data, 7) ? rgb_t::white() : rgb_t::black();
				bitmap.pix32(y * 9 + i, x * 6 + 1) = BIT(data, 6) ? rgb_t::white() : rgb_t::black();
				bitmap.pix32(y * 9 + i, x * 6 + 2) = BIT(data, 5) ? rgb_t::white() : rgb_t::black();
				bitmap.pix32(y * 9 + i, x * 6 + 3) = BIT(data, 4) ? rgb_t::white() : rgb_t::black();
				bitmap.pix32(y * 9 + i, x * 6 + 4) = BIT(data, 3) ? rgb_t::white() : rgb_t::black();
				bitmap.pix32(y * 9 + i, x * 6 + 5) = BIT(data, 2) ? rgb_t::white() : rgb_t::black();
			}
		}
	}

	return 0;
}

void informer_213_state::vram_start_addr_w(offs_t offset, uint8_t data)
{
	if (offset)
		m_vram_start_addr = (m_vram_start_addr & 0xff00) | (data << 0);
	else
		m_vram_start_addr = (m_vram_start_addr & 0x00ff) | (data << 8);

	if (offset)
		logerror("vram_start_addr_w: %04x\n", m_vram_start_addr);
}

void informer_213_state::vram_end_addr_w(offs_t offset, uint8_t data)
{
	if (offset)
		m_vram_end_addr = (m_vram_end_addr & 0xff00) | (data << 0);
	else
		m_vram_end_addr = (m_vram_end_addr & 0x00ff) | (data << 8);

	if (offset)
		logerror("vram_end_addr_w: %04x\n", m_vram_end_addr);
}

void informer_213_state::vram_start_addr2_w(offs_t offset, uint8_t data)
{
	if (offset)
		m_vram_start_addr2 = (m_vram_start_addr2 & 0xff00) | (data << 0);
	else
		m_vram_start_addr2 = (m_vram_start_addr2 & 0x00ff) | (data << 8);

	if (offset)
		logerror("vram_start_addr2_w: %04x\n", m_vram_start_addr2);
}

void informer_213_state::cursor_start_w(uint8_t data)
{
	logerror("cursor_start_w: %02x\n", data);

	// 76------  unknown
	// --5-----  cursor visible
	// ---4----  unknown
	// ----3210  cursor starting line, values seen: 0 (block/off), 6 (underline)

	m_cursor_start = data;
}

void informer_213_state::cursor_end_w(uint8_t data)
{
	logerror("cursor_end_w: %02x\n", data);

	// 7654----  unknown
	// ----3210  cursor ending line, values seen: 9 (block), 8 (underline), 1 (off)

	m_cursor_end = data;
}

void informer_213_state::cursor_addr_w(offs_t offset, uint8_t data)
{
	if (offset)
		m_cursor_addr = (m_cursor_addr & 0xff00) | (data << 0);
	else
		m_cursor_addr = (m_cursor_addr & 0x00ff) | (data << 8);

	if (offset)
		logerror("cursor_addr_w: %04x\n", m_cursor_addr);
}

void informer_213_state::screen_ctrl_w(uint8_t data)
{
	logerror("screen_ctrl_w: %02x\n", data);

	// 76------  unknown
	// --5-----  screen reverse
	// ---4----  cursor blink/steady
	// ----3210  unknown

	m_screen_ctrl = data;
}

void informer_213_state::vblank_w(int state)
{
	if (state)
	{
		m_vector = 0x10;
		m_maincpu->set_input_line(M6809_FIRQ_LINE, HOLD_LINE);
	}
}

static const gfx_layout char_layout =
{
	6,9,
	RGN_FRAC(1,1),
	1,
	{ 0 },
	{ 0, 1, 2, 3, 4, 5 },
	{ 0*8, 1*8, 2*8, 3*8, 4*8, 5*8, 6*8, 7*8, 8*8 },
	8*16
};

static GFXDECODE_START(chars)
	GFXDECODE_ENTRY("chargen", 0, char_layout, 0, 1)
GFXDECODE_END


//**************************************************************************
//  MACHINE EMULATION
//**************************************************************************

uint8_t informer_213_state::unk_42_r()
{
	logerror("unk_42_r\n");
	return m_unk_42 | 4;
}

void informer_213_state::unk_42_w(uint8_t data)
{
	logerror("unk_42_w: %02x\n", data);
	m_unk_42 = data;
}

void informer_213_state::bell_w(uint8_t data)
{
	logerror("bell_w: %02x\n", data);

	// 76543---  unknown
	// -----2--  beeper
	// ------10  unknown

	m_beep->set_state(BIT(data, 2));
}

void informer_213_state::kbd_int_w(int state)
{
	m_vector = 0x14;
	m_maincpu->set_input_line(M6809_FIRQ_LINE, HOLD_LINE);
}

uint8_t informer_213_state::vector_r()
{
	uint8_t tmp = m_vector;
	m_vector = 0x00;

	return tmp;
}

void informer_213_state::machine_start()
{
	// register for save states
	save_item(NAME(m_vram_start_addr));
	save_item(NAME(m_vram_start_addr2));
	save_item(NAME(m_vram_end_addr));
	save_item(NAME(m_cursor_addr));
	save_item(NAME(m_screen_ctrl));
	save_item(NAME(m_unk_42));
	save_item(NAME(m_vector));
}

void informer_213_state::machine_reset()
{
	m_vector = 0x00;
}


//**************************************************************************
//  MACHINE DEFINTIONS
//**************************************************************************

void informer_213_state::informer_213(machine_config &config)
{
	MC6809(config, m_maincpu, 18.432_MHz_XTAL / 4); // unknown clock
	m_maincpu->set_addrmap(AS_PROGRAM, &informer_213_state::mem_map);

	SCC8530N(config, m_scc, 0); // unknown clock

	// video
	SCREEN(config, m_screen, SCREEN_TYPE_LCD);
	m_screen->set_color(rgb_t::amber());
	m_screen->set_size(480, 234);
	m_screen->set_visarea_full();
	m_screen->set_refresh_hz(60);
	m_screen->set_vblank_time(ATTOSECONDS_IN_USEC(2500)); // not accurate
//  m_screen->set_raw(18.432_MHz_XTAL, 0, 0, 0, 0, 0, 0);
	m_screen->set_screen_update(FUNC(informer_213_state::screen_update));
	m_screen->screen_vblank().set(FUNC(informer_213_state::vblank_w));

	PALETTE(config, m_palette, palette_device::MONOCHROME);

	GFXDECODE(config, "gfxdecode", m_palette, chars);

	// sound
	SPEAKER(config, "mono").front_center();
	BEEP(config, "beep", 500).add_route(ALL_OUTPUTS, "mono", 0.50); // frequency unknown

	informer_213_kbd_hle_device &kbd(INFORMER_213_KBD_HLE(config, "kbd"));
	kbd.int_handler().set(FUNC(informer_213_state::kbd_int_w));
}


//**************************************************************************
//  ROM DEFINITIONS
//**************************************************************************

ROM_START( in213 )
	ROM_REGION(0x8000, "maincpu", 0)
	// 79687-305  PTF02 SNA  V2.6 CK=24EE (checksum matches)
	ROM_LOAD("79687-305.bin", 0x0000, 0x8000, CRC(0638c6d6) SHA1(1906f835f255d595c5743b453614ba21acb5acae))

	ROM_REGION(0x2000, "chargen", 0)
	// 79688-003  ICT 213/CG.  CK=C4E0 (checksum matches)
	ROM_LOAD("79688-003.bin", 0x0000, 0x2000, CRC(75e0da94) SHA1(c10c71fcf980a5f868a85bc264661183fa69fa72))
ROM_END

ROM_START( in213ae )
	ROM_REGION(0x8000, "maincpu", 0)
	// 79750-304-213AE_V1.6_CK-B1B8
	ROM_LOAD("79750-304.bin", 0x0000, 0x8000, CRC(82ffe69e) SHA1(3803100aeb8f5e484bc9f4c533ef4f25223c9023))

	ROM_REGION(0x4000, "chargen", 0)
	// 79747-002  V.32 ME C.G.  V3.1 CK=D68C (checksum matches)
	ROM_LOAD("79747-002.bin", 0x0000, 0x4000, CRC(7425327f) SHA1(e3e67305b3b8936683724d1347a451fffe96bf0e))
ROM_END


//**************************************************************************
//  SYSTEM DRIVERS
//**************************************************************************

//    YEAR  NAME     PARENT   COMPAT  MACHINE       INPUT         CLASS               INIT        COMPANY     FULLNAME           FLAGS
COMP( 1990, in213  , 0,       0,      informer_213, informer_213, informer_213_state, empty_init, "Informer", "Informer 213",    MACHINE_NOT_WORKING | MACHINE_SUPPORTS_SAVE )
COMP( 1992, in213ae, 0,       0,      informer_213, informer_213, informer_213_state, empty_init, "Informer", "Informer 213 AE", MACHINE_NOT_WORKING | MACHINE_SUPPORTS_SAVE )
