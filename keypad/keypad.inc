// Date/Time editing keypad

#constant kpad_img "keypad.gci"
#constant kpad_dat "keypad.dat"

// object indexes into ImageControl
#CONST
    iKeyPad      // offset 0x0
    iKeyPad_0    // offset 0x13600
    iKeyPad_1    // offset 0x17400
    iKeyPad_2    // offset 0x1B200
    iKeyPad_3    // offset 0x1D200
    iKeyPad_4    // offset 0x1F200
    iKeyPad_5    // offset 0x21200
    iKeyPad_6    // offset 0x24E00
    iKeyPad_7    // offset 0x26E00
    iKeyPad_8    // offset 0x28E00
    iKeyPad_9    // offset 0x2AE00
    iKeyPad_10   // offset 0x2CE00
    iKeyPad_11   // offset 0x2EE00
    iKeyPad_12   // offset 0x30E00
    iKeyPad_13   // offset 0x34A00
#END

// Number of entries
#constant KbButtons iKeyPad_13
// Keypad position on screen
#constant xoffs 250
#constant yoffs 30

// Keypad values array
#DATA
    byte kPadKeys 0x74, 0x64, 0x31, 0x32, 0x33, 0x73, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x65, 0x30
#END

