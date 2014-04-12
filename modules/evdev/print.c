/**
 * @file print.c Input event device info
 *
 * Copyright (C) 2010 Creytiv.com
 */
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <linux/input.h>
#include <re.h>
#include <baresip.h>
#include "print.h"


#define test_bit(bit, array)    (array[bit/8] & (1<<(bit%8)))


/**
 * Print the name information
 *
 * @param fd Device file descriptor
 */
void print_name(int fd)
{
	char name[256]= "Unknown";

	if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) < 0) {
		perror("evdev ioctl");
	}

	info("evdev: device name: %s\n", name);
}


/**
 * Print supported events
 *
 * @param fd Device file descriptor
 */
void print_events(int fd)
{
	uint8_t evtype_bitmask[EV_MAX/8 + 1];
	int i;

	memset(evtype_bitmask, 0, sizeof(evtype_bitmask));
	if (ioctl(fd, EVIOCGBIT(0, EV_MAX), evtype_bitmask) < 0) {
		warning("evdev: ioctl EVIOCGBIT (%m)\n", errno);
		return;
	}

	printf("Supported event types:\n");

	for (i = 0; i < EV_MAX; i++) {
		if (!test_bit(i, evtype_bitmask))
			continue;

		printf("  Event type 0x%02x ", i);

		switch (i) {

		case EV_KEY :
			printf(" (Keys or Buttons)\n");
			break;
		case EV_REL :
			printf(" (Relative Axes)\n");
			break;
		case EV_ABS :
			printf(" (Absolute Axes)\n");
			break;
		case EV_MSC :
			printf(" (Something miscellaneous)\n");
			break;
		case EV_LED :
			printf(" (LEDs)\n");
			break;
		case EV_SND :
			printf(" (Sounds)\n");
			break;
		case EV_REP :
			printf(" (Repeat)\n");
			break;
		case EV_FF :
			printf(" (Force Feedback)\n");
			break;
		default:
			printf(" (Unknown event type: 0x%04x)\n", i);
			break;
		}
	}
}


/**
 * Print supported keys
 *
 * @param fd Device file descriptor
 */
void print_keys(int fd)
{
	uint8_t key_bitmask[KEY_MAX/8 + 1];
	int i;

	memset(key_bitmask, 0, sizeof(key_bitmask));
	if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bitmask)),
		  key_bitmask) < 0) {
		perror("evdev ioctl");
	}

	printf("Supported Keys:\n");

	for (i = 0; i < KEY_MAX; i++) {
		if (!test_bit(i, key_bitmask))
			continue;

		printf("  Key  0x%02x ", i);

		switch (i) {

		case KEY_RESERVED : printf(" (Reserved)\n"); break;
		case KEY_ESC : printf(" (Escape)\n"); break;
		case KEY_1 : printf(" (1)\n"); break;
		case KEY_2 : printf(" (2)\n"); break;
		case KEY_3 : printf(" (3)\n"); break;
		case KEY_4 : printf(" (4)\n"); break;
		case KEY_5 : printf(" (5)\n"); break;
		case KEY_6 : printf(" (6)\n"); break;
		case KEY_7 : printf(" (7)\n"); break;
		case KEY_8 : printf(" (8)\n"); break;
		case KEY_9 : printf(" ()\n"); break;
		case KEY_0 : printf(" ()\n"); break;
		case KEY_MINUS : printf(" (-)\n"); break;
		case KEY_EQUAL : printf(" (=)\n"); break;
		case KEY_BACKSPACE : printf(" (Backspace)\n"); break;
		case KEY_TAB : printf(" (Tab)\n"); break;
		case KEY_Q : printf(" (Q)\n"); break;
		case KEY_W : printf(" (W)\n"); break;
		case KEY_E : printf(" (E)\n"); break;
		case KEY_R : printf(" (R)\n"); break;
		case KEY_T : printf(" (T)\n"); break;
		case KEY_Y : printf(" (Y)\n"); break;
		case KEY_U : printf(" (U)\n"); break;
		case KEY_I : printf(" (I)\n"); break;
		case KEY_O : printf(" (O)\n"); break;
		case KEY_P : printf(" (P)\n"); break;
		case KEY_LEFTBRACE : printf(" ([)\n"); break;
		case KEY_RIGHTBRACE : printf(" (])\n"); break;
		case KEY_ENTER : printf(" (Enter)\n"); break;
		case KEY_LEFTCTRL : printf(" (LH Control)\n"); break;
		case KEY_A : printf(" (A)\n"); break;
		case KEY_S : printf(" (S)\n"); break;
		case KEY_D : printf(" (D)\n"); break;
		case KEY_F : printf(" (F)\n"); break;
		case KEY_G : printf(" (G)\n"); break;
		case KEY_H : printf(" (H)\n"); break;
		case KEY_J : printf(" (J)\n"); break;
		case KEY_K : printf(" (K)\n"); break;
		case KEY_L : printf(" (L)\n"); break;
		case KEY_SEMICOLON : printf(" (;)\n"); break;
		case KEY_APOSTROPHE : printf(" (')\n"); break;
		case KEY_GRAVE : printf(" (`)\n"); break;
		case KEY_LEFTSHIFT : printf(" (LH Shift)\n"); break;
		case KEY_BACKSLASH : printf(" (\\)\n"); break;
		case KEY_Z : printf(" (Z)\n"); break;
		case KEY_X : printf(" (X)\n"); break;
		case KEY_C : printf(" (C)\n"); break;
		case KEY_V : printf(" (V)\n"); break;
		case KEY_B : printf(" (B)\n"); break;
		case KEY_N : printf(" (N)\n"); break;
		case KEY_M : printf(" (M)\n"); break;
		case KEY_COMMA : printf(" (,)\n"); break;
		case KEY_DOT : printf(" (.)\n"); break;
		case KEY_SLASH : printf(" (/)\n"); break;
		case KEY_RIGHTSHIFT : printf(" (RH Shift)\n"); break;
		case KEY_KPASTERISK : printf(" (*)\n"); break;
		case KEY_LEFTALT : printf(" (LH Alt)\n"); break;
		case KEY_SPACE : printf(" (Space)\n"); break;
		case KEY_CAPSLOCK : printf(" (CapsLock)\n"); break;
		case KEY_F1 : printf(" (F1)\n"); break;
		case KEY_F2 : printf(" (F2)\n"); break;
		case KEY_F3 : printf(" (F3)\n"); break;
		case KEY_F4 : printf(" (F4)\n"); break;
		case KEY_F5 : printf(" (F5)\n"); break;
		case KEY_F6 : printf(" (F6)\n"); break;
		case KEY_F7 : printf(" (F7)\n"); break;
		case KEY_F8 : printf(" (F8)\n"); break;
		case KEY_F9 : printf(" (F9)\n"); break;
		case KEY_F10 : printf(" (F10)\n"); break;
		case KEY_NUMLOCK : printf(" (NumLock)\n"); break;
		case KEY_SCROLLLOCK : printf(" (ScrollLock)\n"); break;
		case KEY_KP7 : printf(" (KeyPad 7)\n"); break;
		case KEY_KP8 : printf(" (KeyPad 8)\n"); break;
		case KEY_KP9 : printf(" (Keypad 9)\n"); break;
		case KEY_KPMINUS : printf(" (KeyPad Minus)\n"); break;
		case KEY_KP4 : printf(" (KeyPad 4)\n"); break;
		case KEY_KP5 : printf(" (KeyPad 5)\n"); break;
		case KEY_KP6 : printf(" (KeyPad 6)\n"); break;
		case KEY_KPPLUS : printf(" (KeyPad Plus)\n"); break;
		case KEY_KP1 : printf(" (KeyPad 1)\n"); break;
		case KEY_KP2 : printf(" (KeyPad 2)\n"); break;
		case KEY_KP3 : printf(" (KeyPad 3)\n"); break;
		case KEY_KPDOT : printf(" (KeyPad decimal point)\n"); break;
/*		case KEY_103RD : printf(" (Huh?)\n"); break; */
		case KEY_F13 : printf(" (F13)\n"); break;
		case KEY_102ND : printf(" (Beats me...)\n"); break;
		case KEY_F11 : printf(" (F11)\n"); break;
		case KEY_F12 : printf(" (F12)\n"); break;
		case KEY_F14 : printf(" (F14)\n"); break;
		case KEY_F15 : printf(" (F15)\n"); break;
		case KEY_F16 : printf(" (F16)\n"); break;
		case KEY_F17 : printf(" (F17)\n"); break;
		case KEY_F18 : printf(" (F18)\n"); break;
		case KEY_F19 : printf(" (F19)\n"); break;
		case KEY_F20 : printf(" (F20)\n"); break;
		case KEY_KPENTER : printf(" (Keypad Enter)\n"); break;
		case KEY_RIGHTCTRL : printf(" (RH Control)\n"); break;
		case KEY_KPSLASH : printf(" (KeyPad Forward Slash)\n"); break;
		case KEY_SYSRQ : printf(" (System Request)\n"); break;
		case KEY_RIGHTALT : printf(" (RH Alternate)\n"); break;
		case KEY_LINEFEED : printf(" (Line Feed)\n"); break;
		case KEY_HOME : printf(" (Home)\n"); break;
		case KEY_UP : printf(" (Up)\n"); break;
		case KEY_PAGEUP : printf(" (Page Up)\n"); break;
		case KEY_LEFT : printf(" (Left)\n"); break;
		case KEY_RIGHT : printf(" (Right)\n"); break;
		case KEY_END : printf(" (End)\n"); break;
		case KEY_DOWN : printf(" (Down)\n"); break;
		case KEY_PAGEDOWN : printf(" (Page Down)\n"); break;
		case KEY_INSERT : printf(" (Insert)\n"); break;
		case KEY_DELETE : printf(" (Delete)\n"); break;
		case KEY_MACRO : printf(" (Macro)\n"); break;
		case KEY_MUTE : printf(" (Mute)\n"); break;
		case KEY_VOLUMEDOWN : printf(" (Volume Down)\n"); break;
		case KEY_VOLUMEUP : printf(" (Volume Up)\n"); break;
		case KEY_POWER : printf(" (Power)\n"); break;
		case KEY_KPEQUAL : printf(" (KeyPad Equal)\n"); break;
		case KEY_KPPLUSMINUS : printf(" (KeyPad +/-)\n"); break;
		case KEY_PAUSE : printf(" (Pause)\n"); break;
		case KEY_F21 : printf(" (F21)\n"); break;
		case KEY_F22 : printf(" (F22)\n"); break;
		case KEY_F23 : printf(" (F23)\n"); break;
		case KEY_F24 : printf(" (F24)\n"); break;
		case KEY_KPCOMMA : printf(" (KeyPad comma)\n"); break;
		case KEY_LEFTMETA : printf(" (LH Meta)\n"); break;
		case KEY_RIGHTMETA : printf(" (RH Meta)\n"); break;
		case KEY_COMPOSE : printf(" (Compose)\n"); break;
		case KEY_STOP : printf(" (Stop)\n"); break;
		case KEY_AGAIN : printf(" (Again)\n"); break;
		case KEY_PROPS : printf(" (Properties)\n"); break;
		case KEY_UNDO : printf(" (Undo)\n"); break;
		case KEY_FRONT : printf(" (Front)\n"); break;
		case KEY_COPY : printf(" (Copy)\n"); break;
		case KEY_OPEN : printf(" (Open)\n"); break;
		case KEY_PASTE : printf(" (Paste)\n"); break;
		case KEY_FIND : printf(" (Find)\n"); break;
		case KEY_CUT : printf(" (Cut)\n"); break;
		case KEY_HELP : printf(" (Help)\n"); break;
		case KEY_MENU : printf(" (Menu)\n"); break;
		case KEY_CALC : printf(" (Calculator)\n"); break;
		case KEY_SETUP : printf(" (Setup)\n"); break;
		case KEY_SLEEP : printf(" (Sleep)\n"); break;
		case KEY_WAKEUP : printf(" (Wakeup)\n"); break;
		case KEY_FILE : printf(" (File)\n"); break;
		case KEY_SENDFILE : printf(" (Send File)\n"); break;
		case KEY_DELETEFILE : printf(" (Delete File)\n"); break;
		case KEY_XFER : printf(" (Transfer)\n"); break;
		case KEY_PROG1 : printf(" (Program 1)\n"); break;
		case KEY_PROG2 : printf(" (Program 2)\n"); break;
		case KEY_WWW : printf(" (Web Browser)\n"); break;
		case KEY_MSDOS : printf(" (DOS mode)\n"); break;
		case KEY_COFFEE : printf(" (Coffee)\n"); break;
		case KEY_DIRECTION : printf(" (Direction)\n"); break;
		case KEY_CYCLEWINDOWS : printf(" (Window cycle)\n"); break;
		case KEY_MAIL : printf(" (Mail)\n"); break;
		case KEY_BOOKMARKS : printf(" (Book Marks)\n"); break;
		case KEY_COMPUTER : printf(" (Computer)\n"); break;
		case KEY_BACK : printf(" (Back)\n"); break;
		case KEY_FORWARD : printf(" (Forward)\n"); break;
		case KEY_CLOSECD : printf(" (Close CD)\n"); break;
		case KEY_EJECTCD : printf(" (Eject CD)\n"); break;
		case KEY_EJECTCLOSECD : printf(" (Eject / Close CD)\n"); break;
		case KEY_NEXTSONG : printf(" (Next Song)\n"); break;
		case KEY_PLAYPAUSE : printf(" (Play and Pause)\n"); break;
		case KEY_PREVIOUSSONG : printf(" (Previous Song)\n"); break;
		case KEY_STOPCD : printf(" (Stop CD)\n"); break;
		case KEY_RECORD : printf(" (Record)\n"); break;
		case KEY_REWIND : printf(" (Rewind)\n"); break;
		case KEY_PHONE : printf(" (Phone)\n"); break;
		case KEY_ISO : printf(" (ISO)\n"); break;
		case KEY_CONFIG : printf(" (Config)\n"); break;
		case KEY_HOMEPAGE : printf(" (Home)\n"); break;
		case KEY_REFRESH : printf(" (Refresh)\n"); break;
		case KEY_EXIT : printf(" (Exit)\n"); break;
		case KEY_MOVE : printf(" (Move)\n"); break;
		case KEY_EDIT : printf(" (Edit)\n"); break;
		case KEY_SCROLLUP : printf(" (Scroll Up)\n"); break;
		case KEY_SCROLLDOWN : printf(" (Scroll Down)\n"); break;
		case KEY_KPLEFTPAREN : printf(" (KeyPad LH paren)\n"); break;
		case KEY_KPRIGHTPAREN : printf(" (KeyPad RH paren)\n"); break;
#if 0
		case KEY_INTL1 : printf(" (Intl 1)\n"); break;
		case KEY_INTL2 : printf(" (Intl 2)\n"); break;
		case KEY_INTL3 : printf(" (Intl 3)\n"); break;
		case KEY_INTL4 : printf(" (Intl 4)\n"); break;
		case KEY_INTL5 : printf(" (Intl 5)\n"); break;
		case KEY_INTL6 : printf(" (Intl 6)\n"); break;
		case KEY_INTL7 : printf(" (Intl 7)\n"); break;
		case KEY_INTL8 : printf(" (Intl 8)\n"); break;
		case KEY_INTL9 : printf(" (Intl 9)\n"); break;
		case KEY_LANG1 : printf(" (Language 1)\n"); break;
		case KEY_LANG2 : printf(" (Language 2)\n"); break;
		case KEY_LANG3 : printf(" (Language 3)\n"); break;
		case KEY_LANG4 : printf(" (Language 4)\n"); break;
		case KEY_LANG5 : printf(" (Language 5)\n"); break;
		case KEY_LANG6 : printf(" (Language 6)\n"); break;
		case KEY_LANG7 : printf(" (Language 7)\n"); break;
		case KEY_LANG8 : printf(" (Language 8)\n"); break;
		case KEY_LANG9 : printf(" (Language 9)\n"); break;
#endif
		case KEY_PLAYCD : printf(" (Play CD)\n"); break;
		case KEY_PAUSECD : printf(" (Pause CD)\n"); break;
		case KEY_PROG3 : printf(" (Program 3)\n"); break;
		case KEY_PROG4 : printf(" (Program 4)\n"); break;
		case KEY_SUSPEND : printf(" (Suspend)\n"); break;
		case KEY_CLOSE : printf(" (Close)\n"); break;
		case KEY_UNKNOWN : printf(" (Specifically unknown)\n"); break;
#ifdef KEY_BRIGHTNESSDOWN
		case KEY_BRIGHTNESSDOWN: printf(" (Brightness Down)\n");break;
#endif
#ifdef KEY_BRIGHTNESSUP
		case KEY_BRIGHTNESSUP : printf(" (Brightness Up)\n"); break;
#endif
		case BTN_0 : printf(" (Button 0)\n"); break;
		case BTN_1 : printf(" (Button 1)\n"); break;
		case BTN_2 : printf(" (Button 2)\n"); break;
		case BTN_3 : printf(" (Button 3)\n"); break;
		case BTN_4 : printf(" (Button 4)\n"); break;
		case BTN_5 : printf(" (Button 5)\n"); break;
		case BTN_6 : printf(" (Button 6)\n"); break;
		case BTN_7 : printf(" (Button 7)\n"); break;
		case BTN_8 : printf(" (Button 8)\n"); break;
		case BTN_9 : printf(" (Button 9)\n"); break;
		case BTN_LEFT : printf(" (Left Button)\n"); break;
		case BTN_RIGHT : printf(" (Right Button)\n"); break;
		case BTN_MIDDLE : printf(" (Middle Button)\n"); break;
		case BTN_SIDE : printf(" (Side Button)\n"); break;
		case BTN_EXTRA : printf(" (Extra Button)\n"); break;
		case BTN_FORWARD : printf(" (Forward Button)\n"); break;
		case BTN_BACK : printf(" (Back Button)\n"); break;
		case BTN_TRIGGER : printf(" (Trigger Button)\n"); break;
		case BTN_THUMB : printf(" (Thumb Button)\n"); break;
		case BTN_THUMB2 : printf(" (Second Thumb Button)\n"); break;
		case BTN_TOP : printf(" (Top Button)\n"); break;
		case BTN_TOP2 : printf(" (Second Top Button)\n"); break;
		case BTN_PINKIE : printf(" (Pinkie Button)\n"); break;
		case BTN_BASE : printf(" (Base Button)\n"); break;
		case BTN_BASE2 : printf(" (Second Base Button)\n"); break;
		case BTN_BASE3 : printf(" (Third Base Button)\n"); break;
		case BTN_BASE4 : printf(" (Fourth Base Button)\n"); break;
		case BTN_BASE5 : printf(" (Fifth Base Button)\n"); break;
		case BTN_BASE6 : printf(" (Sixth Base Button)\n"); break;
		case BTN_DEAD : printf(" (Dead Button)\n"); break;
		case BTN_A : printf(" (Button A)\n"); break;
		case BTN_B : printf(" (Button B)\n"); break;
		case BTN_C : printf(" (Button C)\n"); break;
		case BTN_X : printf(" (Button X)\n"); break;
		case BTN_Y : printf(" (Button Y)\n"); break;
		case BTN_Z : printf(" (Button Z)\n"); break;
		case BTN_TL : printf(" (Thumb Left Button)\n"); break;
		case BTN_TR : printf(" (Thumb Right Button )\n"); break;
		case BTN_TL2 : printf(" (Second Thumb Left Button)\n"); break;
		case BTN_TR2 : printf(" (Second Thumb Right Button )\n");
			break;
		case BTN_SELECT : printf(" (Select Button)\n"); break;
		case BTN_MODE : printf(" (Mode Button)\n"); break;
		case BTN_THUMBL : printf(" (Another Left Thumb Button )\n");
			break;
		case BTN_THUMBR : printf(" (Another Right Thumb Button )\n");
			break;
		case BTN_TOOL_PEN : printf(" (Digitiser Pen Tool)\n"); break;
		case BTN_TOOL_RUBBER : printf(" (Digitiser Rubber Tool)\n");
			break;
		case BTN_TOOL_BRUSH : printf(" (Digitiser Brush Tool)\n");
			break;
		case BTN_TOOL_PENCIL : printf(" (Digitiser Pencil Tool)\n");
			break;
		case BTN_TOOL_AIRBRUSH:printf(" (Digitiser Airbrush Tool)\n");
			break;
		case BTN_TOOL_FINGER : printf(" (Digitiser Finger Tool)\n");
			break;
		case BTN_TOOL_MOUSE : printf(" (Digitiser Mouse Tool)\n");
			break;
		case BTN_TOOL_LENS : printf(" (Digitiser Lens Tool)\n"); break;
		case BTN_TOUCH : printf(" (Digitiser Touch Button )\n"); break;
		case BTN_STYLUS : printf(" (Digitiser Stylus Button )\n");
			break;
		case BTN_STYLUS2: printf(" (Second Digitiser Stylus Btn)\n");
			break;
#ifdef KEY_NUMERIC_0
		case KEY_NUMERIC_0: printf(" (Numeric 0)\n");
			break;
#endif
#ifdef KEY_NUMERIC_1
		case KEY_NUMERIC_1: printf(" (Numeric 1)\n");
			break;
#endif
#ifdef KEY_NUMERIC_2
		case KEY_NUMERIC_2: printf(" (Numeric 2)\n");
			break;
#endif
#ifdef KEY_NUMERIC_3
		case KEY_NUMERIC_3: printf(" (Numeric 3)\n");
			break;
#endif
#ifdef KEY_NUMERIC_4
		case KEY_NUMERIC_4: printf(" (Numeric 4)\n");
			break;
#endif
#ifdef KEY_NUMERIC_5
		case KEY_NUMERIC_5: printf(" (Numeric 5)\n");
			break;
#endif
#ifdef KEY_NUMERIC_6
		case KEY_NUMERIC_6: printf(" (Numeric 6)\n");
			break;
#endif
#ifdef KEY_NUMERIC_7
		case KEY_NUMERIC_7: printf(" (Numeric 7)\n");
			break;
#endif
#ifdef KEY_NUMERIC_8
		case KEY_NUMERIC_8: printf(" (Numeric 8)\n");
			break;
#endif
#ifdef KEY_NUMERIC_9
		case KEY_NUMERIC_9: printf(" (Numeric 9)\n");
			break;
#endif
#ifdef KEY_NUMERIC_STAR
		case KEY_NUMERIC_STAR: printf(" (Numeric *)\n");
			break;
#endif
#ifdef KEY_NUMERIC_POUND
		case KEY_NUMERIC_POUND: printf(" (Numeric #)\n");
			break;
#endif
		default:
			printf(" (Unknown key)\n");
		}
	}
}


/**
 * Print supported LEDs
 *
 * @param fd Device file descriptor
 */
void print_leds(int fd)
{
	uint8_t led_bitmask[LED_MAX/8 + 1];
	int i;

	memset(led_bitmask, 0, sizeof(led_bitmask));
	if (ioctl(fd, EVIOCGBIT(EV_LED, sizeof(led_bitmask)),
		  led_bitmask) < 0) {
		perror("evdev ioctl");
	}

	printf("Supported LEDs:\n");

	for (i = 0; i < LED_MAX; i++) {
		if (!test_bit(i, led_bitmask))
			continue;

		printf("  LED type 0x%02x ", i);

		switch (i) {

		case LED_NUML :
			printf(" (Num Lock)\n");
			break;
		case LED_CAPSL :
			printf(" (Caps Lock)\n");
			break;
		case LED_SCROLLL :
			printf(" (Scroll Lock)\n");
			break;
		case LED_COMPOSE :
			printf(" (Compose)\n");
			break;
		case LED_KANA :
			printf(" (Kana)\n");
			break;
		case LED_SLEEP :
			printf(" (Sleep)\n");
			break;
		case LED_SUSPEND :
			printf(" (Suspend)\n");
			break;
		case LED_MUTE :
			printf(" (Mute)\n");
			break;
		case LED_MISC :
			printf(" (Miscellaneous)\n");
			break;
		default:
			printf(" (Unknown LED type: 0x%04x)\n", i);
		}
	}
}
