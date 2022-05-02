/**
 * Based on cgc2wav from Attila Grósz
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "getopt.h"

static int turboMode = 0; // 0 - no tudbo mode, 1 -turbo mode, loader not writed, wav in normal mode, 2 - turbo mode, wav in turbo mode

const unsigned char turbo4312H = 26; // 45; // A load loop értéke
static int turboBaud = 2900; // 2000; // load loop beállítása utáni baud

const unsigned char default4312H = 105; // A loader loop default értéke
const int defaultBaud = 1150;

/* WAV file header structure */
/* should be 1-byte aligned */
#pragma pack(1)
struct wav_header {
    char           riff[ 4 ];       // 4 bytes
    unsigned int   rLen;            // 4 bytes
    char           WAVE[ 4 ];       // 4 bytes
    char           fmt[ 4 ];        // 4 bytes
    unsigned int   fLen;            /* 0x1020 */
    unsigned short wFormatTag;      /* 0x0001 */
    unsigned short nChannels;       /* 0x0001 */
    unsigned int   nSamplesPerSec;
    unsigned int   nAvgBytesPerSec; // nSamplesPerSec*nChannels*(nBitsPerSample%8)
    unsigned short nBlockAlign;     /* 0x0001 */
    unsigned short nBitsPerSample;  /* 0x0008 */
    char           datastr[ 4 ];    // 4 bytes
    unsigned int   data_size;       // 4 bytes
} wave = {
    'R','I','F','F', //     Chunk ID - konstans, 4 byte hosszú, értéke 0x52494646, ASCII kódban "RIFF"
    0,               //     Chunk Size - 4 byte hosszú, a fájlméretet tartalmazza bájtokban a fejléccel együtt, értéke 0x01D61A72 (decimálisan 30808690, vagyis a fájl mérete ~30,8 MB)
    'W','A','V','E', //     Format - konstans, 4 byte hosszú,értéke 0x57415645, ASCII kódban "WAVE"
    'f','m','t',' ', //     SubChunk1 ID - konstans, 4 byte hosszú, értéke 0x666D7420, ASCII kódban "fmt "
    16,              //     SubChunk1 Size - 4 byte hosszú, a fejléc méretét tartalmazza, esetünkben 0x00000010
    1,               //     Audio Format - 2 byte hosszú, PCM esetében 0x0001
    1,               //     Num Channels - 2 byte hosszú, csatornák számát tartalmazza, esetünkben 0x0002
    44100,           //     Sample Rate - 4 byte hosszú, mintavételezési frekvenciát tartalmazza, esetünkben 0x00007D00 (decimálisan 32000)
    44100,           //     Byte Rate - 4 byte hosszú, értéke 0x0000FA00 (decmálisan 64000)
    1,               //     Block Align - 2 byte hosszú, az 1 mintában található bájtok számát tartalmazza - 0x0002
    8,               //     Bits Per Sample - 2 byte hosszú, felbontást tartalmazza bitekben, értéke 0x0008
    'd','a','t','a', //     Sub Chunk2 ID - konstans, 4 byte hosszú, értéke 0x64617461, ASCII kódban "data"
    0                //     Sub Chunk2 Size - 4 byte hosszú, az adatblokk méretét tartalmazza bájtokban, értéke 0x01D61A1E
};
#pragma pack()

static unsigned char    p_gain = 6 * 0x0f;
const unsigned char     p_silence = 0;
static unsigned int     wav_sample_count = 0;
static unsigned char    level; // 0 vagy 1?

static unsigned int wav_baud = defaultBaud;

static double bauds_to_samples( unsigned int bauds ) { return ( (double)wave.nSamplesPerSec / bauds ); }
static unsigned int cycles_to_samples( unsigned int cycles ) { return ( cycles * wave.nSamplesPerSec) / 2216750; }

static void filter_output( unsigned char level, FILE *wavfile ) {
    static double hp_accu = 0, lp_accu = 0;
    double in = level * 1.0;
    double clipped = lp_accu - hp_accu;
    unsigned char out;

    if ( clipped >=127.0 ) {
        clipped = 127.0;
    } else if ( clipped<-127.0 ) {
        clipped = -127;
    }

    out = ((unsigned char)(clipped)) ^ 0x80;
    lp_accu += ((double)in - lp_accu) * 0.5577;
    hp_accu += (lp_accu - hp_accu) * 0.0070984;
    fputc( out, wavfile);
}

static void dump_bit(FILE *fp, unsigned int bit) {
    unsigned int j;
    unsigned int period = (unsigned int)( bauds_to_samples( wav_baud ) / ( bit + 1 ) );

    do {
        for (j=0; j<period ; j++) {
            filter_output(level ? p_silence : p_gain , fp);
        }
        level ^= 1;
    } while (bit--);
}

static void close_wav( FILE *outfile ) {
    int full_size = ftell( outfile );
    fseek( outfile, 4, SEEK_SET );
    fwrite( &full_size, sizeof( full_size ), 1, outfile ); // Wave header 2. field : filesize with header. First the lowerest byte

    int data_size = full_size - sizeof( wave );
    fseek( outfile, sizeof( wave ) - 4 ,SEEK_SET ); // data chunk size position
    fwrite( &data_size, sizeof( data_size ), 1, outfile );

    fclose(outfile);
}

static unsigned char output_wav_byte( FILE *wavfile, unsigned char byte ) {
    unsigned int bc = 7;
    do {
        dump_bit( wavfile, (byte >> bc)&1 );
    } while (bc--);
    return byte;
}

static void write_silence(FILE *wavfile) {
    unsigned int j;
    for( j=0; j<cycles_to_samples(15000); j++ ) {
        filter_output( p_silence, wavfile );
    }
}

static void init_wav( FILE *wavfile ) {
    fwrite( &wave, sizeof( wave ), 1, wavfile );
    /* Lead in silence */
    write_silence( wavfile );
    level = 0;
}

// load turbo4312H to 4312H memory
static void insert_turbo_loader_block( FILE* wav, unsigned char loop, int new_baud ) {
    output_wav_byte( wav, 0x3C ); // System Data Record Type
    output_wav_byte( wav, 0x01 ); // Size
    unsigned char checksum = output_wav_byte( wav, 0x12 ); // Addres lower byte
    checksum += output_wav_byte( wav, 0x43 ); // Address higher byte
    checksum += output_wav_byte( wav, loop ); // Data
    wav_baud = new_baud;
    output_wav_byte( wav, checksum );
}

// Elhelyezi a Turbo loading szöveget a 4416H címre
static void insert_turbo_design_block( FILE* wav ) {
    char design[] = "Turbo loading";
    unsigned char size = sizeof( design ) - 1; // Leave ending 0
    output_wav_byte( wav, 0x3C ); // System Data Record Type
    output_wav_byte( wav, size ); // Size
    unsigned char checksum = output_wav_byte( wav, 0x16 ); // Addres lower byte
    checksum += output_wav_byte( wav, 0x44 ); // Address higher byte
    for( int i=0; i<size; i++ ) {
        checksum += output_wav_byte( wav, design[ i ] ); // Data
    }
    output_wav_byte( wav, checksum );
}

static void convert( FILE *tap, FILE* wav ) {
    unsigned char byte;
    int counter = 0;
    fseek( tap, 0L, SEEK_END );
    int tapSize = ftell( tap );
    int posEntry = tapSize - 3; // Entry block
    fseek( tap, 0, SEEK_SET );
    while ( !feof( tap ) ) {
        byte = fgetc( tap );
        if ( turboMode ) {
            if ( counter == 256 ) { // 0x55 SYSTEM esetén
                if ( byte != 0x55 ) { // Not SYSTEM tape
                    fprintf( stderr, "No system tap file! Turbo mode disabled.\n" );
                    turboMode = 0;
                }
            } else if ( counter == 263 ) {
                if ( byte == 0x3C ) { // first data block
                    insert_turbo_loader_block( wav, turbo4312H, turboBaud );
                    turboMode = 2;
                    insert_turbo_design_block( wav );
                } else {
                    fprintf( stderr, "Not system tap! Turbo mode disabled.\n" );
                    turboMode = 0;
                }
            } else if ( counter == posEntry ) {
                if ( byte == 0x78 ) { // Last entry block
                    insert_turbo_loader_block( wav, default4312H, defaultBaud );
                    turboMode = 0;
                } else {
                    fprintf( stderr, "Invalid system tap! Entry not found!\n" );
                    exit(1);
                }
            }
            counter++;
        }
        output_wav_byte( wav, byte );
    }
    write_silence( wav );
    int size = ftell( wav ) / 1024;
    fprintf( stdout, "%i kbytes written.\n", size );
    fclose( tap );
}

static void print_usage() {
    printf( "tap2wav v0.1b\n");
    printf( "Colour Genie binary tap to PCM wave file converter.\n");
    printf( "Copyright 2022 by László Princz\n");
    printf( "Usage:\n");
    printf( "tap2wav -i <input_filename> -o <output_filename>\n");
    printf( "Command line option:\n");
    printf( "-g <gain> : gain, must be between 1 and 7 (default: 6)\n");
    printf( "-b <baud> : baud (dafault 1150 )\n");
    printf( "-t        : turbo mode, system only (%d baud with loader)\n", turboBaud );
    printf( "-h        : prints this text\n");
    exit(1);
}

int main(int argc, char *argv[]) {
    int finished = 0;
    int arg1;
    FILE *tapFile = 0, *wav = 0;

    while (!finished) {
        switch (getopt (argc, argv, "?htf:i:o:g:b:")) {
            case -1:
            case ':':
                finished = 1;
                break;
            case '?':
            case 'h':
                print_usage();
                break;
            case 't':
                turboMode = 1;
                break;
            case 'f':
                if ( !sscanf( optarg, "%i", &arg1 ) ) {
                    fprintf( stderr, "Error parsing argument for '-f'.\n");
                    exit(2);
                } else {
                    if ( arg1!=48000 && arg1!=44100 && arg1!=22050 &&
                        arg1!=11025 && arg1!=8000 ) {
                            fprintf( stderr, "Unsupported sample rate: %i.\n", arg1);
                            fprintf( stderr, "Supported sample rates are: 48000, 44100, 22050, 11025 and 8000.\n");
                            exit(3);
                    }
                    wave.nSamplesPerSec = arg1;
                    wave.nAvgBytesPerSec = wave.nSamplesPerSec*wave.nChannels*(wave.nBitsPerSample%8);
                }
                break;
            case 'g':
                if ( !sscanf( optarg, "%i", &arg1 ) ) {
                    fprintf( stderr, "Error parsing argument for '-g'.\n");
                    exit(2);
                } else {
                    if ( arg1<0 || arg1>7 ) {
                        fprintf( stderr, "Illegal gain value: %i.\n", arg1);
                        fprintf( stderr, "Gain must be between 1 and 8.\n");
                    }
                    p_gain = arg1*0x0f;
                }
                break;
            case 'b':
                if ( !sscanf( optarg, "%i", &arg1 ) ) {
                    fprintf( stderr, "Error parsing argument for '-g'.\n");
                    exit(2);
                } else {
                    if ( arg1<0 || arg1>10000 ) {
                        fprintf( stderr, "Illegal baud value: %i.\n", arg1);
                        exit(3);
                    }
                    wav_baud = arg1;
                }
                break;
            case 'i':
                if ( !(tapFile = fopen( optarg, "rb")) ) {
                    fprintf( stderr, "Error opening %s.\n", optarg);
                    exit(4);
                }
                break;
            case 'o':
                if ( !(wav = fopen( optarg, "wb")) ) {
                    fprintf( stderr, "Error creating %s.\n", optarg);
                    exit(4);
                }
                break;
            default:
                break;
        }
    }

    if ( !tapFile ) {
        print_usage();
    } else if ( !wav ) {
        print_usage();
    } else {
        init_wav( wav );
        convert( tapFile, wav );
        close_wav( wav );
    }

    return 0;
}
