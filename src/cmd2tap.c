/**
 * This utility converts the z88dk output .cmd fileformat to .tap format.
 * Cmd format information comes from trs-80 cmd format : https://raw.githubusercontent.com/schnitzeltony/z80/master/src/cmd2cas.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "getopt.h"


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
void convert_object_record( FILE *cmd, FILE *tap ) {
    int pos = ftell( cmd ); // info only
    unsigned char size8 = fgetc( cmd ) - 2; //  + 254;
    int size = size8 ? size8 : 256;
    unsigned char byte; // tmp byte variable
    unsigned int address;
    fblockread( &address, 2, cmd ); // xx, yy
    write_system_data_block( cmd, tap, address, size ); // Copy size bytes from cmd to tap
    fprintf( stdout, "%d object byte converted to 0x%04X from 0x%06X\n", size, address, pos );
}

//Record Type 02 – Transfer Address
//    02 nn xx zz …
//    nn = length of the block followed by the applicable data. xx = LSB, yyy = MSB
//
//    02 is usually the last header of the file.
void convert_transfer_record( FILE *cmd, FILE *tap ) {
    unsigned char byte; // tmp byte variable
    unsigned char size = fgetc( cmd );
    unsigned int address;
    fblockread( &address, 2, cmd ); // xx, yy
    if ( size == 2 ) {
        write_system_entry_block( tap, address );
    } else {
        fprintf( stderr, "ENTRY: invalid transfer size (%#x)\n", size);
    }
    fprintf( stdout, "%d transfer byte converted to %04X\n", size, address );
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

void convert( FILE *cmd, FILE *tap ) {
    unsigned char byte; // tmp byte variable
    while ( ( byte = fgetc( cmd ) ) && ( !feof( cmd ) ) ) {
        int pos = ftell( cmd );
        switch( byte ) { // Record type check
            case 0x05 :
                convert_filename_record( cmd, tap );
                break;
            case 0x01 :
                if ( !system_name_position ) system_name_position = write_tap_header( tap );
                convert_object_record( cmd, tap );
                break;
            case 0x02 :
                convert_transfer_record( cmd, tap );
                break;
            default :
                // convert_comment_record( cmd );
                fprintf( stderr, "Invalid CMD record type 0x%02X at position 0x%06X\n", byte, pos );
                exit( 1 );
        }
    }
    fclose( cmd );
    fclose( tap );
}

void print_usage() {
    printf( "cmd2tap v0.1b\n");
    printf( "Convert Colour Genie cmd file to binary tap format.\n");
    printf( "Copyright 2022 by László Princz\n");
    printf( "Usage:\n");
    printf( "cmd2tap [options] -i <cmd_filename> -o <tap_filename>\n");
    printf( "Command line option:\n");
    printf( "-n <name> : Programname override. Need if cmd file not contents filename record.\n");
    exit(1);
}

int main(int argc, char *argv[]) {
    int opt = 0;
    int i = 0;
    FILE *cmdFile = 0;
    FILE *tapFile = 0;

    while ( ( opt = getopt (argc, argv, "?h:i:o:n:") ) != -1 ) {
        switch ( opt ) {
            case -1:
            case ':':
                break;
            case '?':
            case 'h':
                print_usage();
                break;
            case 'n': // system name override
                for( i=0; i<6 && optarg[ i ]; i++ ) system_name[ i ] = optarg[ i ];
                for( int j = i; j < 7; j++ ) system_name[ i ] = 0;
                break;
            case 'i': // open cmd file
                if ( !( cmdFile = fopen( optarg, "rb" ) ) ) {
                    fprintf( stderr, "Error opening %s.\n", optarg);
                    exit(4);
                }
                break;
            case 'o': // create tap file
                if ( !( tapFile = fopen( optarg, "wb" ) ) ) {
                    fprintf( stderr, "Error creating %s.\n", optarg);
                    exit(4);
                }
                break;
            default:
                break;
        }
    }

    if ( tapFile && cmdFile ) {
        convert( cmdFile, tapFile );
        fprintf( stdout, "Ok\n" );
    } else {
        print_usage();
    }
    return 0;
}
