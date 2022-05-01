# eg2000utils
EACA EG2000 Colour Genie utilities

## cas2tap
Convert .cas file to .tap file. It not handles DATA .cas format (PRINT#-1).
.tap file is a binary mirrored .wav file.
Many .cas file fomrat exist for EG2000 emulators. The .tap file is for only generate the .wav file.
This utility checks the input .cas, .cgc or .tap file, and generates .tap file.
It can rename the stored program name.
The Color Genie documentation contains bad information from tape structure. It contains the TRS80 informations.
options:
-r <name> : Rename the program in tap file. 

## cdm2tap
Convert the z88dk output .cmd fileformat to .tap format.
Cmd format information comes from trs-80 cmd format : https://raw.githubusercontent.com/schnitzeltony/z80/master/src/cmd2cas.c
options:
-r <name> : Add (or override) name the program in tap file. The z88dk output cmd file not contains name record.

## tap2wav
Convert .tap format to wav. The wav file is usable direct to CLOAD or SYSTEM command on Colour Genie.
options:
-b <baud> : The default data speed on tape is 1200 baud. This option overrides it. If befor loading you change one byte, on Colour Genie, it can read faster.
  On Color Genie:
    POKE 17170, 25 : CLOAD or SYSTEM can load the 3000 baud wav file.
    POKE 17170, 105 : CLOAD or SYSTEM can load the default 1200 baud wav file.
- t : Turbo wav file. The program will be loaded with 3000 baud! Not need modificaton on EG2000 before load!
