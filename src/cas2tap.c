/**
 * EACA EG2000 Colour Genie .cas file convert to .tap file.
 * This code not handles DATA .cas format (PRINT#-1).
 * .tap file is a binary mirrored .wav file.
 * Many .cas file fomrat exist for EG2000 emulators. The .tap file is for only generate the .wav file.
 * This utility checks the input .cas, .cgc or .tap file, and generates .tap file.
 * It can rename the stored program name.
 * The Color Genie documentation contains bad information from tape structure. It contains the TRS80 informations.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "getopt.h"

const int MainVersion = 0;
const int SubVersion = 3;

// Help for correct leader information : eg2000 - basicrom.pdf

int body_only = 0; // If true, then leader does not write into .tap file
unsigned char new_name[ 7 ] = { 0,0,0,0,0,0,0 }; // The new program name, if not empty

void fblockread( void *bytes, size_t size, FILE *src ) {
    int pos = ftell( src );

    int ret = fread( bytes, size, 1, src );
    if ( ret != 1 ) {
        fprintf( stderr, "File block read error 2 at pos 0x%04X\n", pos );
    } else {
        fprintf( stderr, "File block %d bytes read ok at pos 0x%04X\n", size, pos );
    }

//    while ( ( fread( bytes, size, 1, src ) != 1 ) && ( !feof( src ) ) ) fseek( src, pos, SEEK_SET );
    if ( feof( src ) ) {
        fprintf( stderr, "File block read error at pos 0x%04X\n", pos );
        exit(1);
    }
}

/**
 * The EG2000 technical manual contains the TRS80 format.
 * TRS80 leader :  256 x 0x00 + 0x5A
 * EG2000 leader : 255 x 0xAA + 0x66
 */
void write_leading( FILE *tap ) {
    if ( !body_only ) {
        unsigned char bytes[] = { 0xAA, 0x66 }; // The correct data
        for( int i=0; i<255; i++ ) fwrite( &bytes[0], 1, 1, tap );
        fwrite( &bytes[1], 1, 1, tap );
    }
}

void test_header( FILE *cas, FILE *tap ) {
    unsigned int size = 0;
    unsigned char byte = 0; // tmp byte variable
    fseek( cas, 0, SEEK_SET );
    unsigned char header_string[ 33 ];
    fblockread( header_string, 32, cas );
    if ( strncmp( header_string, "Colour Genie - Virtual Tape File", 32 ) == 0 ) { // Standard EG2000 emulator cas file with header
        while ( fgetc( cas ) && !feof( cas ) ); // 0x00-ig olvas
        if ( fgetc( cas ) == 0x66 ) { // 0x00,0x66 : end of header 
            size = ftell( cas ); // Header size
            fprintf( stdout, "Standard EG2000 emulator cas file header\n" );
        } else {
            fprintf( stderr, "Invalid emulator header end, 0x66 byte not found. %02X found at %d. bytes\n", byte, size );
        }
    } else if ( header_string[ 0 ] == 0x00 || header_string[ 0 ] == 0xAA ) { // TRS-80 or EG2000 header
        fseek( cas, 0, SEEK_SET );
        int cnt = 0;
        while ( ( ( byte = fgetc( cas ) ) == header_string[ 0 ] ) && ( !feof( cas ) ) ) cnt++;
        size = ftell( cas ); // Header size
        if ( feof( cas ) ) { // Invalid header
            fprintf( stderr, "Invalid header. End of file before header ending.\n" );
            exit(1);
        } else if ( byte == 0xA5 ) { // TRS-80 leader sync byte
            if ( cnt == 256 ) {
                fprintf( stdout, "Standard TRS-80 binary tape cas file header\n" );
            } else {
                fprintf( stdout, "Invalid TRS-80 binary tape cas file header. %d 0x00 in leader (not 256).\n", cnt );
            }
        } else if ( byte = 0x66 ) { // EG2000 leader sync byte
            if ( cnt == 255 ) {
                fprintf( stdout, "Standard EG2000 binary tape cas file header\n" );
            } else {
                fprintf( stdout, "Invalid EG2000 binary tape cas file header. %d 0x66 in leader (not 255).\n", cnt );
            }
        } else {
            fprintf( stderr, "Invalid header end, 0xA5 or 0x66 byte not found. %02X found at %d. bytes\n", byte, size );
            exit(1);
        }
    } else {
        size = 0;
        if ( feof( cas ) ) {
            fprintf( stderr, "Invalid EOF\n" );
        }
        fseek( cas, 1, SEEK_SET );
        if ( header_string[ 0 ] == 0x66 ) { // OK, 
            fprintf( stdout, "Headerless EG2000 cas file\n" );
        } else if ( header_string[ 0 ] == 0x5A ) { // OK, 
            fprintf( stdout, "Headerless TRS80 cas file\n" );
        } else {
            header_string[ 32 ] = 0;
            fprintf( stderr, "Invalid header first byte: %02X (%s)\n", header_string[ 0 ], header_string );
            exit(1);
        }
    }
    fprintf( stdout, "Header size is %d bytes\n", size );
    if ( tap ) write_leading( tap );
}

void test_basic_tap( FILE *cas, FILE *tap, unsigned char name_first_char ) {
    fprintf( stdout, "BASIC type .cas file\n" );
    unsigned char byte; // tmp byte variable

    fprintf( stdout, "Basic program. The first character of the name is %c\n", name_first_char );
    if ( tap ) {
        if ( new_name[ 0 ] ) {
            name_first_char = new_name[ 0 ];
            fprintf( stdout, "Renamed to %c\n", name_first_char );
        }
        fwrite( &name_first_char, 1, 1, tap );
    }
    int size = 0;
    int finished = 0;
    int nullCounter = 0;
    int uidChecksum = 0; // Unique checksum for whole program
    while ( !finished ) {
        byte = fgetc( cas );
        uidChecksum += byte;
        if ( feof( cas ) ) {
            fprintf( stderr, "Invalid BASIC block. End not found.\n" );
            exit(1);
            finished = 1;
        } else {
            if ( tap ) fwrite( &byte, 1, 1, tap );
            size++;
            if ( byte ) {
                nullCounter = 0;
            } else {
                nullCounter++;
            }
            if ( nullCounter == 3 ) {
                finished = 1;
            }
        }
    }
    byte = fgetc( cas );
    int dummy_counter = 0;
    while ( !feof( cas ) ) {
        if ( !dummy_counter++ ) {
            int pos = ftell( cas ) - 1;
            fprintf( stderr, "Drop data ( 0x%02X ) after end of BASIC program from position 0x%04X\n", byte, pos );
        } else {
            fprintf( stderr, "Drop data 0x%02X\n", byte );
        }
        byte = fgetc( cas );
    }
    fprintf( stdout, "Basic program size is %d bytes\n", size );
    fprintf( stdout, "Unique code id: C%dC%d\n", size, uidChecksum );
}

void test_system_filename_block( FILE *cas, FILE *tap ) {
    unsigned char name[ 7 ] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
    fblockread( &name, 6, cas );
    fprintf( stdout, "SYSTEM program name: '%s'\n", name );
    if ( tap ) {
        if ( new_name[ 0 ] ) {
            fprintf( stdout, "Renamed to %s\n", new_name );
            fwrite( &new_name, 1, 6, tap );
        } else {
            fwrite( &name, 1, 6, tap );
        }
    }
}

void test_system_entry_block( FILE *cas, FILE *tap ) {
    unsigned char byte = 0; // tmp byte variable
    int address = 0;
    fblockread( &address, 2, cas );
    if ( tap ) fwrite( &address, 1, 2, tap );
    fprintf( stdout, "SYSTEM entry point: '%04X'\n", address );    
}


int test_system_data_block( FILE *cas, FILE *tap, int *uidChecksum ) {
    unsigned char byte = 0; // tmp byte variable
    unsigned char sum = 0;
    uint address = 0;
    int pos = ftell( cas ) - 1; // 3C pos

    unsigned int size = fgetc( cas );
    if ( tap ) fwrite( &size, 1, 1, tap );
    if ( size == 0 ) size = 256;

    fblockread( &address, 2, cas );
    sum = address/256 + address%256;
    if ( tap ) fwrite( &address, 1, 2, tap );

    int cnt = 0;
    for( cnt=0; cnt<size && !feof( cas ); cnt++ ) {
        byte = fgetc( cas );
        sum += byte;
        *uidChecksum += byte;
        if ( tap ) fwrite( &byte, 1, 1, tap );
    }
    unsigned char checksum = fgetc( cas );
    if ( cnt == size ) {
        if ( sum == checksum ) {
            if ( tap ) fwrite( &checksum, 1, 1, tap );
            fprintf( stdout, "%d bytes in SYSTEM DATA block from 0x%04X. Checksum ok (%02X)\n", size, pos, checksum );
        } else {
            int cpos = ftell( cas ) - 1; // 3C pos
            fprintf( stdout, "%d bytes in SYSTEM DATA block from 0x%04X. Invalid checksum at position 0x%04X\n", size, pos, cpos );
            fprintf( stderr, "Chekcsum is %02X, sum=%02X, size=%d\n", checksum, sum, cnt );
            exit(1);
        }
    } else {
        fprintf( stderr, "Invalid SYSTEM DATA block size. %d in header, but %d bytes founded\n", size, cnt );
        exit(1);
    }
    return size;
}

void test_system_tap( FILE *cas, unsigned char first, FILE *tap ) {
    fprintf( stdout, "SYSTEM type .cas file\n" );
    int codeSize = 0;
    int uidChecksum = 0;
    int last_block_type = 0; // 0: befor blocks, 1: after filename block, 2: after data block, 3: after entry block
    int dummy_counter = 0;
    while ( !feof( cas ) ) {
        if ( first == 0x55 ) {
            if ( last_block_type ) {
                int pos = ftell( cas ) - 1;
                fprintf( stderr, "Two filename block in SYSTEM tape at position 0x%04X\n", pos );
                exit(1);
            } else {
                if ( tap ) fwrite( &first, 1, 1, tap );
                test_system_filename_block( cas, tap );
                last_block_type = 1;
            }
        } else if ( first == 0x3C ) {
            if ( last_block_type ) {
                if ( tap ) fwrite( &first, 1, 1, tap );
                codeSize += test_system_data_block( cas, tap, &uidChecksum );
                last_block_type = 2;
            } else {
                int pos = ftell( cas ) - 1;
                fprintf( stderr, "SYSTEM DATA block before filename block at position 0x%04X\n", pos );
                exit(1);
            }
        } else if ( first == 0x78 ) {
            if ( tap ) fwrite( &first, 1, 1, tap );
            test_system_entry_block( cas, tap );
            last_block_type = 3;
        } else if ( last_block_type == 3 ) {
            dummy_counter++;
            int pos = ftell( cas ) - 1;
            fprintf( stderr, "Drop data after end of SYSTEM program: 0x%02X at 0x%04X\n", first, pos );
        } else if ( last_block_type == 2 ) { // The ROM skip bytes after SYSTEM block, if it is not 3CH or 78
            dummy_counter++;
            int pos = ftell( cas ) - 1;
            fprintf( stderr, "Drop data after SYSTEM DATA block: 0x%02X at 0x%04X\n", first, pos );
        } else {
            int pos = ftell( cas ) - 1;
            fprintf( stderr, "Invalid SYSTEM block type code: %02X at position 0x%04X\n", first, pos );
            exit(1);
        }
        if ( !feof( cas ) ) {
            first = fgetc( cas );
        }
    }
    fprintf( stdout, "Unique code id: S%dC%d\n", codeSize, uidChecksum );
}

void test_data_tap( FILE *cas, unsigned char first, FILE *tap ) {
    fprintf( stdout, "DATA type .cas file, with first %02X byte\n", first );
    unsigned char byte = 0; // tmp byte variable
    byte = first;
    int size = 0;
    while ( !feof( cas ) && byte != 0x0D ) {
    }
    if ( byte == 0x0D ) {
        fprintf( stdout, "Data size is %d bytes\n", size );
    } else {
        fprintf( stderr, "Invalid DATA block\n" );
    }
}

void test_cas_body( FILE *cas, FILE *tap ) {
    unsigned char byte = 0; // tmp byte variable
    byte = fgetc( cas );
    if ( byte == 0x55 || byte == 0x3C || byte == 0x78 ) {
        test_system_tap( cas, byte, tap );
    } else { // Basic or data ... de legyen csak BASIC
        test_basic_tap( cas, tap, byte ); // A név első karaktere a byte változóban
// @TODO: What is the different between BASIC and DATA tape format?
//    } else {
//        if ( tap ) fwrite( &byte, 1, 1, tap );
//        test_data_tap( cas, byte, tap );
    }
}

void test_cas_file( FILE *cas, FILE *tap ) {
    test_header( cas, tap );
    test_cas_body( cas, tap );
    fclose( cas );
    if ( tap ) fclose( tap );
}

void print_usage() {
    printf( "cas2tap v%d.%db (build: %s)\n", MainVersion, SubVersion, __DATE__ );
    printf( "Test and convert Colour Genie and TRS-80 CAS, cgt or binary tap file to binary tap.\n");
    printf( "Copyright 2022 by László Princz\n");
    printf( "Usage:\n");
    printf( "cas2tap [options] -i <cas_filename> [ -o <tap_filename> ]\n");
    printf( "Command line option:\n");
    printf( "-b            : body only, leave leading (for test only)\n");
    printf( "-r <new_name> : rename programfile\n");
    exit(1);
}

int main(int argc, char *argv[]) {
    int opt = 0;
    FILE *casFile = 0;
    FILE *tapFile = 0;

    while ( ( opt = getopt (argc, argv, "b?h:i:r:o:") ) != -1 ) {
        switch ( opt ) {
            case -1:
            case ':':
                break;
            case '?':
            case 'h':
                print_usage();
                break;
            case 'b':
                body_only = 1;
                break;
            case 'r': // rename
                for( int i=0; i<6 && optarg[i]; i++ ) new_name[ i ] = optarg[ i ];
                break;
            case 'i': // open cas file
                if ( !( casFile = fopen( optarg, "rb") ) ) {
                    fprintf( stderr, "Error opening %s.\n", optarg);
                    exit(4);
                }
                break;
            case 'o': // create tap file
                if ( !(tapFile = fopen( optarg, "wb")) ) {
                    fprintf( stderr, "Error creating %s.\n", optarg);
                    exit(4);
                }
                break;
            default:
                break;
        }
    }

    if ( casFile ) {
        test_cas_file( casFile, tapFile );
        fprintf( stdout, "Ok\n" );
    } else {
        print_usage();
    }
    return 0;
}
