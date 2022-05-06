/**
 * This utility converts the z88dk output .cmd fileformat to .tap format.
 * Cmd format information comes from trs-80 cmd format : https://raw.githubusercontent.com/schnitzeltony/z80/master/src/cmd2cas.c
 * CMD record types:
 * 0x01 : DATA block
 * 0x02 : ENTRY block
 * 0x05 : NAME block
 * others : COMMENT? block
 */
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "getopt.h"
#include <libgen.h>

#define VM 0
#define VS 4
#define VB 'b'

int verbose = 0;
char system_name[ 7 ] = { 0,0,0,0,0,0,0 }; // Name for SYSTEM tape, if Cmd format not includes program name.
int system_name_position = 0; // If it is not 0, then program name already writed.

void fblockread( void *bytes, size_t size, FILE *src ) {
    int pos = ftell( src );
    while ( ( fread( bytes, size, 1, src ) != 1 ) && ( !feof( src ) ) ) fseek( src, pos, SEEK_SET );
    if ( feof( src ) ) {
        fprintf( stderr, "File block read error\n" );
        exit(1);
    }
}

void write_leader( FILE *tap ) {
    unsigned char bytes[] = { 0xAA, 0x66 };
    for( int i=0; i<255; i++ ) fwrite( &bytes[0], 1, 1, tap );
    fwrite( &bytes[1], 1, 1, tap );
}

int write_system_filename_block( FILE *tap ) {
    if ( system_name[ 0 ] ) { // Name from program option
        unsigned char byte = 0x55;
        fwrite( &byte, 1, 1, tap ); // record type: 0x55
        int pos = ftell( tap );
        fwrite( &system_name, 1, 6, tap );
        fprintf( stdout, "SYSTEM program name: '%s'\n", system_name );    
        return pos;
    } else {
        fprintf( stderr, "SYSTEM programname not defined!\n" );
        exit( 1 );
    }
}

void write_system_data_block( FILE *cmd, FILE *tap, int address, int counter ) {
    unsigned char byte = 0x3C;
    fwrite( &byte, 1, 1, tap ); // record type: 0x3C

    // put size on one byte
    unsigned char size8 = ( counter > 255 ) ? ( counter - 256 ) : counter;
    fwrite( &size8, 1, 1, tap );

    // put two bytes of address
    fwrite( &address, 1, 2, tap );

    // init checksum
    unsigned char checksum = 0;
    checksum = address/256 + address % 256;

    // put data
    int cnt = 0;
    for( cnt=0; cnt<counter && !feof( cmd ); cnt++ ) {
        byte = fgetc( cmd );
        fwrite( &byte, 1, 1, tap );
        checksum += byte;
    }
    if ( cnt == counter ) {
        fwrite( &checksum, 1, 1, tap );
    } else {
        fprintf( stdout, "object record write error.\n" );
        exit(1);
    }
}

void write_system_entry_block( FILE *tap, int address ) {
    unsigned char byte = 0x78;
    fwrite( &byte, 1, 1, tap ); // record type: 0x78
    fwrite( &address, 1, 2, tap );
    fprintf( stdout, "SYSTEM entry point: '%04X'\n", address );
}

int write_tap_header( FILE *tap ) {
    write_leader( tap );
    return write_system_filename_block( tap );
}

// Record Type 05 – Filename
//    05 nn xx yy zz …
//    nn = length of the filename block followed by the applicable data.
void convert_filename_record( FILE *cmd, FILE *tap ) {
    unsigned char nn = fgetc( cmd );
    unsigned char filename[ nn+1 ];
    fblockread( &filename, nn, cmd );
    filename[ nn ] = 0;
    fprintf( stdout, "Filename: %s\n", filename );
}

//Record Type 01 – Object Code / Load Block
//    01 nn xx yy zz …
//    nn = 00, 01, or 02 meaning that following the next 2 bytes (xx and yy) of the loading address, are followed either by a block of 254, 255, or 256 bytes.
//
//    For example, A 01 02 00 6E xx yy zz would mean to set up the load block, indicate that the address for the block is 6E00, and that 256 bytes will follow.
//    Another example, A 01 01 00 6E xx yy zz would mean to set up the load block, indicate that the address for the block is 6E00, and that 255 bytes will follow.
void convert_load_record( FILE *cmd, FILE *tap, unsigned char sizeByte, int address, int pos ) {
    int size = sizeByte ? sizeByte : 256;
    write_system_data_block( cmd, tap, address, size ); // Copy size bytes from cmd to tap
    if ( verbose ) fprintf( stdout, "%d object bytes converted to 0x%04X from 0x%06X\n", size, address, pos );
}

//Record Type 02  - Last block is only 4 bytes!
// Ignore the size byte value. Ignore all bytes after last block
void convert_last_record( FILE *cmd, FILE *tap, unsigned char sizeByte, int address, int pos ) {
    unsigned char byte; // tmp byte variable
    write_system_entry_block( tap, address );
/*
    int cnt = sizeByte;
    for( cnt = sizeByte; cnt && !feof( cmd ); cnt-- ) fgetc( cmd ); // Skip 
    if ( feof( cmd ) && sizeByte == 254 ) { // SKIP EOF
        fprintf( stderr, "Transfer block size 0 = 254, but no data found before eof! Ignore block data\n" );
    } else if ( cnt > 0 ) {
        fprintf( stderr, "ENTRY: invalid transfer block size (%02X , %d) at position 0x%04X, not enough bytes. need %d\n", sizeByte, sizeByte, pos, cnt );
        exit(1);
    }
    fprintf( stdout, "%d transfer bytes converted to 0x%04X address\n", sizeByte, address );
*/
}

//Record Type other – Comment
//    xx nn …
//    nn = length of the block
void convert_comment_record( FILE *cmd ) {
    unsigned char byte; // tmp byte variable
    unsigned char size = fgetc( cmd );
    for( int i=0; i<size; i++ ) {
        byte = fgetc( cmd );
    }
    fprintf( stdout, "%d comment byte skipped.\n", size );
}

/**
 * recordTypes:
 * 1 - load block
 * 2 - last block
 * 3 - ignore block
 */
void convert_system( unsigned char recordType, FILE *cmd, FILE *tap ) {
    int finished = 0;
    while ( !finished && !feof( cmd ) ) {
        int pos = ftell( cmd ) -1; // Block start pos
        // The block size
        unsigned char sizeByte = fgetc( cmd ) - 2;
        // The load address
        unsigned int address = 0;
        fblockread( &address, 2, cmd ); // xx, yy
        // 
        switch( recordType ) { // Record type check
//            case 0x05 : // filename in TRS80 CMD
//                convert_filename_record( cmd, tap );
//                break;
            case 0x01 : // Load data block
                if ( !system_name_position ) system_name_position = write_tap_header( tap );
                convert_load_record( cmd, tap, sizeByte, address, pos );
                break;
            case 0x02 : // last block
                convert_last_record( cmd, tap, sizeByte, address, pos );
                finished = 1; // Ignore all bytes after last block
                break;
            case 0x03 : // ignore block
//            case 0x00 :
//            case 0xff :
//            case 0x20 : // Comment to end of file?
                finished = 1;
                fprintf( stdout, "Found comment record 0x%02X. SKIP comment from 0x%06X to end of CMD file\n", recordType, pos );
                break;
            default :
                // convert_comment_record( cmd );
                fprintf( stderr, "Invalid CMD record type 0x%02X at position 0x%06X\n", recordType, pos );
                exit( 1 );
        }
        recordType = fgetc( cmd );
    }
}

void convert_basic( FILE *cmd, FILE *tap ) {
    fprintf( stderr, "Basic CMD conversion not implemented yet\n" );
    exit( 1 );
}

void convert( FILE *cmd, FILE *tap ) {
    unsigned char byte = fgetc( cmd ); // tmp byte variable
    if ( !feof( cmd ) ) { // Ok, there is data
        if ( byte == 0xFF ) {
            convert_basic( cmd, tap );
        } else {
            convert_system( byte, cmd, tap );
        }
    } else {
        fprintf( stderr, "CMD file is empty!\n" );
        exit( 1 );
    }
    fclose( cmd );
    fclose( tap );
}

void print_usage() {
    printf( "cmd2tap v%d.%d%c (build: %s)\n", VM, VS, VB, __DATE__ );
    printf( "Convert Colour Genie cmd file to binary tap format.\n");
    printf( "Copyright 2022 by László Princz\n");
    printf( "Usage:\n");
    printf( "cmd2tap [options] -i <cmd_filename> -o <tap_filename>\n");
    printf( "Command line option:\n");
    printf( "-n <name> : Programname. Default the filename.\n");
    printf( "-v        : Verbose mode. Default the non-verbose mode.\n");
    exit(1);
}

void copy_to_name( char* basename ) {
    int i = 0;
    for( i = 0; i<6 && basename[ i ]; i++ ) system_name[ i ] = basename[ i ];
    for( int j = i; j < 7; j++ ) system_name[ j ] = 0;
}

char* copyStr( char *str, int chunkPos ) {
    int size = 0;
    while( str[size++] );
    char *newStr = malloc( size );
    for( int i=0; i<size; i++ ) newStr[i] = str[ i ];
    if ( chunkPos>0 && chunkPos<size ) { // Chunk extension
        if ( newStr[ size - chunkPos ] == '.' ) newStr[ size - chunkPos ] = 0;
    }
    return newStr;
}

char* copyStr3( char *str1, char *str2, char *str3 ) {
    int size1 = 0; while( str1[size1++] );
    int size2 = 0; while( str2[size2++] );
    size2--;
    int size3 = 0; while( str3[size3++] );
    char *newStr = malloc( size1 + size2 + size3 );
    for( int i=0; i<size1; i++ ) newStr[ i ] = str1[ i ];
    if ( size1 ) newStr[ size1 - 1 ] = '/';
    for( int i=0; i<size2; i++ ) newStr[ size1 + i ] = str2[ i ];
    for( int i=0; i<size3; i++ ) newStr[ size1 + size2 + i ] = str3[ i ];
    return newStr;
}

int is_dir( const char *path ) {
    struct stat path_stat;
    stat( path, &path_stat );
    return S_ISDIR( path_stat.st_mode );
}

int main(int argc, char *argv[]) {
    int opt = 0;
    char *srcBasename = 0;
    char *destDir = 0;
    FILE *cmdFile = 0;
    FILE *tapFile = 0;

    while ( ( opt = getopt (argc, argv, "v?h:i:o:n:") ) != -1 ) {
        switch ( opt ) {
            case -1:
            case ':':
                break;
            case '?':
            case 'h':
                print_usage();
                break;
            case 'v':
                verbose = 1;
                break;
            case 'n': // system name override
                copy_to_name( optarg );
                break;
            case 'i': // open cmd file
                if ( !( cmdFile = fopen( optarg, "rb" ) ) ) {
                    fprintf( stderr, "Error opening %s.\n", optarg);
                    exit(4);
                }
                srcBasename = copyStr( basename( optarg ), 5 );
                destDir = copyStr( dirname( optarg ), 0 );
                if ( !system_name[ 0 ] ) copy_to_name( srcBasename );
            break;
            case 'o': // create tap file
                if ( is_dir( optarg ) ) { // Az output egy mappa
                    destDir = copyStr( optarg, 0 );
                } else {
                    if ( !( tapFile = fopen( optarg, "wb" ) ) ) {
                        fprintf( stderr, "Error creating %s.\n", optarg);
                        exit(4);
                    }
                }
                break;
            default:
                break;
        }
    }

    if ( cmdFile ) {
        if ( !tapFile ) { // Nincs megadott tap fájl. destDir biztosan nem null
            char *tapName = copyStr3( destDir, srcBasename, ".tap" );
            if ( !( tapFile = fopen( tapName, "wb" ) ) ) {
                fprintf( stderr, "Error creating %s.\n", tapName );
                exit(4);
            }
            fprintf( stdout, "Create file %s\n", tapName );
        }
        convert( cmdFile, tapFile );
        fprintf( stdout, "Ok\n" );
    } else {
        print_usage();
    }
    return 0;
}
