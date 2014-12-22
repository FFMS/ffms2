//  Copyright (c) 2010-2011 FFmpegSource Project
//
//  Permission is hereby granted, free of charge, to any person obtaining a copy
//  of this software and associated documentation files (the "Software"), to deal
//  in the Software without restriction, including without limitation the rights
//  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//  copies of the Software, and to permit persons to whom the Software is
//  furnished to do so, subject to the following conditions:
//
//  The above copyright notice and this permission notice shall be included in
//  all copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
//  THE SOFTWARE.

#include "ffms.h"
#include "avs_common.h"
#include "ff_filters.h"

#define MAX_CACHE_FILE_LENGTH 512 // Windows API should explode before getting this long

static int default_cache_file( const char *src, const char *user_cache_file, char *out_cache_file )
{
    int ret = 0;
    if( !strcmp( user_cache_file, "" ) )
    {
        strcpy( out_cache_file, src );
        strcat( out_cache_file, ".ffindex" );
    }
    else
    {
        strcpy( out_cache_file, user_cache_file );
        ret = !strcasecmp( src, user_cache_file );
    }
    return ret;
}

static int get_num_logical_cpus()
{
    SYSTEM_INFO SI;
    GetSystemInfo( &SI );
    return SI.dwNumberOfProcessors;
}

static AVS_Value AVSC_CC create_FFIndex( AVS_ScriptEnvironment *env, AVS_Value args, void *user_data )
{
    FFMS_Init( 0, as_bool( as_elt( args, 7 ), 0 ) );
    init_ErrorInfo( ei );

    AVS_Value elt0 = as_elt( args, 0 );
    if( !avs_is_string( elt0 ) )
        return avs_new_value_error( "FFIndex: No source specified" );

    const char *src = as_string( elt0, NULL );
    const char *user_cache_file = as_string( as_elt( args, 1 ), "" );
    int index_mask = as_int( as_elt( args, 2 ), -1 );
    int dump_mask = as_int( as_elt( args, 3 ), 0 );
    const char *audio_file = as_string( as_elt( args, 4 ), "%sourcefile%.%trackzn%.w64" );
    int err_handler = as_int( as_elt( args, 5 ), FFMS_IEH_IGNORE );
    char overwrite = as_bool( as_elt( args, 6 ), 0 );
    const char *demuxer_str = as_string( as_elt( args, 8 ), "default" );

    char cache_file[MAX_CACHE_FILE_LENGTH];
    if( default_cache_file( src, user_cache_file, cache_file ) )
        return avs_new_value_error( "FFIndex: Cache will overwrite the source" );

    if( !strcmp( audio_file, "" ) )
        return avs_new_value_error( "FFIndex: Specifying an empty audio filename is not allowed" );

    int demuxer;
    if( !strcasecmp( demuxer_str, "default" ) )
        demuxer = FFMS_SOURCE_DEFAULT;
    else if( !strcasecmp( demuxer_str, "lavf" ) )
        demuxer = FFMS_SOURCE_LAVF;
    else if( !strcasecmp( demuxer_str, "matroska" ) )
        demuxer = FFMS_SOURCE_MATROSKA;
    else
        return avs_new_value_error( "FFIndex: Invalid demuxer requested" );

    FFMS_Index *index = FFMS_ReadIndex( cache_file, &ei );
    if( overwrite || !index || (index && FFMS_IndexBelongsToFile( index, src, 0 ) != FFMS_ERROR_SUCCESS) )
    {
        FFMS_Indexer *indexer = FFMS_CreateIndexerWithDemuxer( src, demuxer, &ei );
        if( !indexer )
            return avs_new_value_error( ffms_avs_sprintf( "FFIndex: %s", ei.Buffer ) );
        index = FFMS_DoIndexing( indexer, index_mask, dump_mask, FFMS_DefaultAudioFilename,
                                 (void*)audio_file, err_handler, NULL, NULL, &ei );
        if( !index )
            return avs_new_value_error( ffms_avs_sprintf( "FFIndex: %s", ei.Buffer ) );
        if( FFMS_WriteIndex( cache_file, index, &ei ) )
        {
            FFMS_DestroyIndex( index );
            return avs_new_value_error( ffms_avs_sprintf( "FFIndex: %s", ei.Buffer ) );
        }
        FFMS_DestroyIndex( index );
        return avs_new_value_int( 2 - overwrite );
    }
    FFMS_DestroyIndex( index );
    return avs_new_value_int( 0 );
}

static AVS_Value AVSC_CC create_FFVideoSource( AVS_ScriptEnvironment *env, AVS_Value args, void *user_data )
{
    FFMS_Init( 0, as_bool( as_elt( args, 15 ), 0 ) );
    init_ErrorInfo( ei );

    AVS_Value elt0 = as_elt( args, 0 );
    if( !avs_is_string( elt0 ) )
        return avs_new_value_error( "FFVideoSource: No source specified" );

    const char *src = as_string( elt0, NULL );
    int track = as_int( as_elt( args, 1 ), -1 );
    char cache = as_bool( as_elt( args, 2 ), 1 );
    const char *user_cache_file = as_string( as_elt( args, 3 ), "" );
    int fps_num = as_int( as_elt( args, 4 ), -1 );
    int fps_den = as_int( as_elt( args, 5 ), 1 );
    const char *pp = as_string( as_elt( args, 6 ), "" );
    int threads = as_int( as_elt( args, 7 ), -1 );
    const char *timecodes = as_string( as_elt( args, 8 ), "" );
    int seek_mode = as_int( as_elt( args, 9 ), 1 );
    int rff_mode = as_int( as_elt( args, 10 ), 0 );
    int width = as_int( as_elt( args, 11 ), 0 );
    int height = as_int( as_elt( args, 12 ), 0 );
    const char *resizer = as_string( as_elt( args, 13 ), "BICUBIC" );
    const char *csp_name = as_string( as_elt( args, 14 ), "" );
    const char *var_prefix = as_string( as_elt( args, 16 ), "" );

    if( fps_den < 1 )
        return avs_new_value_error( "FFVideoSource: FPS denominator needs to be 1 or higher" );
    if( track <= -2 )
        return avs_new_value_error( "FFVideoSource: No video track selected" );
    if( seek_mode < -1 || seek_mode > 3 )
        return avs_new_value_error( "FFVideoSource: Invalid seekmode selected" );
    if( threads <= 0 )
        threads = get_num_logical_cpus();
    if( rff_mode < 0 || rff_mode > 2 )
        return avs_new_value_error( "FFVideoSource: Invalid RFF mode selected" );
    if( rff_mode > 0 && fps_num > 0 )
        return avs_new_value_error( "FFVideoSource: RFF modes may not be combined with CFR conversion" );
    if( !stricmp( src, timecodes ) )
        return avs_new_value_error( "FFVideoSource: Timecodes will overwrite the source" );

    char cache_file[MAX_CACHE_FILE_LENGTH];
    if( default_cache_file( src, user_cache_file, cache_file ) )
        return avs_new_value_error( "FFVideoSource: Cache will overwrite the source" );

    FFMS_Index *index = NULL;
    if( cache )
    {
        index = FFMS_ReadIndex( cache_file, &ei );
        if( index && *user_cache_file && FFMS_IndexBelongsToFile( index, src, 0 ) != FFMS_ERROR_SUCCESS )
        {
            FFMS_DestroyIndex( index );
            index = NULL;
        }
    }
    if( !index )
    {
        if( !(index = FFMS_MakeIndex( src, 0, 0, NULL, NULL, 1, NULL, NULL, &ei )) )
            return avs_new_value_error( ffms_avs_sprintf( "FFVideoSource: %s", ei.Buffer ) );

        if( cache )
            if( FFMS_WriteIndex( cache_file, index, &ei ) )
            {
                FFMS_DestroyIndex( index );
                return avs_new_value_error( ffms_avs_sprintf( "FFVideoSource: %s", ei.Buffer ) );
            }
    }

    if( track == -1 )
        track = FFMS_GetFirstIndexedTrackOfType( index, FFMS_TYPE_VIDEO, &ei );
    if( track < 0 )
        return avs_new_value_error( "FFVideoSource: No video track found" );

    if( strcmp( timecodes, "" ) )
        if( FFMS_WriteTimecodes( FFMS_GetTrackFromIndex( index, track ), timecodes, &ei ) )
        {
            FFMS_DestroyIndex( index );
            return avs_new_value_error( ffms_avs_sprintf( "FFVideoSource: %s", ei.Buffer ) );
        }

    AVS_Value video = FFVideoSource_create( env, src, track, index, fps_num, fps_den, pp, threads,
        seek_mode, rff_mode, width, height, resizer, csp_name, var_prefix );
    if( avs_is_error( video ) )
        FFMS_DestroyIndex( index );
    return video;
}

static AVS_Value AVSC_CC create_FFAudioSource( AVS_ScriptEnvironment *env, AVS_Value args, void *user_data )
{
    FFMS_Init( 0, as_bool( as_elt( args, 5 ), 0 ) );
    init_ErrorInfo( ei );

    if( !avs_is_string( as_elt( args, 0 ) ) )
        return avs_new_value_error( "FFAudioSource: No source specified" );

    const char *src = as_string( as_elt( args, 0 ), NULL );
    int track = as_int( as_elt( args, 1 ), -1 );
    char cache = as_bool( as_elt( args, 2 ), 1 );
    const char *user_cache_file = as_string( as_elt( args, 3 ), "" );
    int adjust_delay = as_int( as_elt( args, 4 ), -1 );
    const char *var_prefix = as_string( as_elt( args, 6 ), "" );

    if( track <= -2 )
        return avs_new_value_error( "FFAudioSource: No audio track selected" );

    char cache_file[MAX_CACHE_FILE_LENGTH];
    if( default_cache_file( src, user_cache_file, cache_file ) )
        return avs_new_value_error( "FFAudioSource: Cache will overwrite the source" );

    FFMS_Index *index = NULL;
    if( cache )
    {
        index = FFMS_ReadIndex( cache_file, &ei );
        if( index && *user_cache_file && FFMS_IndexBelongsToFile( index, src, 0 ) != FFMS_ERROR_SUCCESS )
        {
            FFMS_DestroyIndex( index );
            index = NULL;
        }
    }

    // Index needs to be remade if it is an unindexed audio track
    if( index && track >= 0 && track < FFMS_GetNumTracks( index ) &&
        FFMS_GetTrackType( FFMS_GetTrackFromIndex( index, track ) ) == FFMS_TYPE_AUDIO &&
        !FFMS_GetNumFrames( FFMS_GetTrackFromIndex( index, track ) ) )
    {
        FFMS_DestroyIndex( index );
        index = NULL;
    }

    // More complicated for finding a default track, reindex the file if at least one audio track exists
    if( index && FFMS_GetFirstTrackOfType( index, FFMS_TYPE_AUDIO, &ei ) >= 0 &&
        FFMS_GetFirstIndexedTrackOfType( index, FFMS_TYPE_AUDIO, &ei ) < 0 )
    {
        int i;
        for( i = 0; i < FFMS_GetNumTracks( index ); i++ )
            if( FFMS_GetTrackType( FFMS_GetTrackFromIndex( index, i ) ) == FFMS_TYPE_AUDIO )
            {
                FFMS_DestroyIndex( index );
                index = NULL;
                break;
            }
    }

    if( !index )
    {
        if( !(index = FFMS_MakeIndex( src, -1, 0, NULL, NULL, 1, NULL, NULL, &ei )) )
            return avs_new_value_error( ffms_avs_sprintf( "FFAudioSource: %s", ei.Buffer ) );

        if( cache )
            if( FFMS_WriteIndex( cache_file, index, &ei ) )
            {
                FFMS_DestroyIndex( index );
                return avs_new_value_error( ffms_avs_sprintf( "FFAudioSource: %s", ei.Buffer ) );
            }
    }

    if( track == -1 )
        track = FFMS_GetFirstIndexedTrackOfType( index, FFMS_TYPE_AUDIO, &ei );
    if( track < 0 )
        return avs_new_value_error( "FFAudioSource: No audio track found" );

    if( adjust_delay < -3 )
        return avs_new_value_error( "FFAudioSource: Invalid delay adjustment specified" );
    if( adjust_delay >= FFMS_GetNumTracks( index ) )
        return avs_new_value_error( "FFAudioSource: Invalid track to calculate delay from specified" );


    AVS_Value audio = FFAudioSource_create( env, src, track, index, adjust_delay, var_prefix );
    if( avs_is_error( audio ) )
        FFMS_DestroyIndex( index );
    return audio;
}

static AVS_Value AVSC_CC create_SWScale( AVS_ScriptEnvironment *env, AVS_Value args, void *user_data )
{
    AVS_Value child = as_elt( args, 0 );
    int dst_width = as_int( as_elt( args, 1 ), 0 );
    int dst_height = as_int( as_elt( args, 2 ), 0 );
    const char* resizer = as_string( as_elt( args, 3 ), "BICUBIC" );
    const char* dst_pix_fmt = as_string( as_elt( args, 4 ), "" );
    return FFSWScale_create( env, child, dst_width, dst_height, resizer, dst_pix_fmt );
}

static AVS_Value AVSC_CC create_FFGetLogLevel( AVS_ScriptEnvironment *env, AVS_Value args, void *user_data )
{ return avs_new_value_int( FFMS_GetLogLevel() ); }

static AVS_Value AVSC_CC create_FFSetLogLevel( AVS_ScriptEnvironment *env, AVS_Value args, void *user_data )
{
    FFMS_SetLogLevel( as_int( as_elt( args, 0 ), 0 ) );
    return avs_new_value_int( FFMS_GetLogLevel() );
}

static AVS_Value AVSC_CC create_FFGetVersion( AVS_ScriptEnvironment *env, AVS_Value args, void *user_data )
{
    int vint = FFMS_GetVersion();
    char *version = ffms_avs_sprintf( "%d.%d.%d.%d", vint >> 24, (vint >> 16) & 0xFF, (vint >> 8) & 0xFF, vint & 0xFF );
    return avs_new_value_string( version );
}

/* the AVS loader for LoadCPlugin */
const char *AVSC_CC avisynth_c_plugin_init( AVS_ScriptEnvironment* env )
{
    /* load the avs library */
    if( ffms_load_avs_lib( env ) )
        return "Failure";
    ffms_avs_lib->avs_add_function( env, "FFIndex", "[source]s[cachefile]s[indexmask]i[dumpmask]i[audiofile]s[errorhandling]i[overwrite]b[utf8]b[demuxer]s", create_FFIndex, 0 );
    ffms_avs_lib->avs_add_function( env, "FFVideoSource", "[source]s[track]i[cache]b[cachefile]s[fpsnum]i[fpsden]i[pp]s[threads]i[timecodes]s[seekmode]i[rffmode]i[width]i[height]i[resizer]s[colorspace]s[utf8]b[varprefix]s", create_FFVideoSource, 0 );
    ffms_avs_lib->avs_add_function( env, "FFAudioSource", "[source]s[track]i[cache]b[cachefile]s[adjustdelay]i[utf8]b[varprefix]s", create_FFAudioSource, 0 );
    ffms_avs_lib->avs_add_function( env, "SWScale", "c[width]i[height]i[resizer]s[colorspace]s", create_SWScale, 0 );
    ffms_avs_lib->avs_add_function( env, "FFGetLogLevel", "", create_FFGetLogLevel, 0 );
    ffms_avs_lib->avs_add_function( env, "FFSetLogLevel", "i", create_FFSetLogLevel, 0 );
    ffms_avs_lib->avs_add_function( env, "FFGetVersion", "", create_FFGetVersion, 0 );

    /* tell avs to call our cleanup method when it closes */
    ffms_avs_lib->avs_at_exit( env, ffms_free_avs_lib, 0 );
    return "FFmpegSource - The Second Coming V2.0 Final";
}

/* the x64 build of avisynth may or may not be wanting this function, depends on who built it */
#ifdef _WIN64
const char *AVSC_CC avisynth_c_plugin_init_s( AVS_ScriptEnvironment* env )
{ return avisynth_c_plugin_init( env ); }
#endif
