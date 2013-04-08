/*
  +----------------------------------------------------------------------+
  | PHP Version 5                                                        |
  +----------------------------------------------------------------------+
  | Copyright (c) 1997-2013 The PHP Group                                |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,      |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.php.net/license/3_01.txt                                  |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Author: Chung Leong <cleong@cal.berkeley.edu>                        |
  +----------------------------------------------------------------------+
*/

/* $Id$ */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_ini.h"
#include "ext/standard/info.h"
#include "php_av.h"

ZEND_DECLARE_MODULE_GLOBALS(av)

/* True global resources - no need for thread safety here */
static int le_av_file;
static int le_av_strm;
static int le_gd = -1;

/* {{{ av_functions[]
 *
 * Every user visible function must have an entry in av_functions[].
 */
const zend_function_entry av_functions[] = {
	PHP_FE(av_file_open,				NULL)
	PHP_FE(av_file_close,				NULL)

	PHP_FE(av_stream_open,				NULL)
	PHP_FE(av_stream_close,				NULL)
	PHP_FE(av_stream_read_image,		NULL)
	PHP_FE(av_stream_write_image,		NULL)

	PHP_FE_END	/* Must be the last line in av_functions[] */
};
/* }}} */

/* {{{ av_module_entry
 */
zend_module_entry av_module_entry = {
#if ZEND_MODULE_API_NO >= 20010901
	STANDARD_MODULE_HEADER,
#endif
	"av",
	av_functions,
	PHP_MINIT(av),
	PHP_MSHUTDOWN(av),
	PHP_RINIT(av),		/* Replace with NULL if there's nothing to do at request start */
	PHP_RSHUTDOWN(av),	/* Replace with NULL if there's nothing to do at request end */
	PHP_MINFO(av),
#if ZEND_MODULE_API_NO >= 20010901
	"0.1", /* Replace with version number for your extension */
#endif
	STANDARD_MODULE_PROPERTIES
};
/* }}} */

#ifdef COMPILE_DL_AV
ZEND_GET_MODULE(av)
#endif

/* {{{ PHP_INI
 */
/* Remove comments and fill if you need to have entries in php.ini
PHP_INI_BEGIN()
    STD_PHP_INI_ENTRY("av.global_value",      "42", PHP_INI_ALL, OnUpdateLong, global_value, zend_av_globals, av_globals)
    STD_PHP_INI_ENTRY("av.global_string", "foobar", PHP_INI_ALL, OnUpdateString, global_string, zend_av_globals, av_globals)
PHP_INI_END()
*/
/* }}} */

/* {{{ php_av_init_globals
 */
/* Uncomment this function if you have INI entries
static void php_av_init_globals(zend_av_globals *av_globals)
{
	av_globals->global_value = 0;
	av_globals->global_string = NULL;
}
*/
/* }}} */

static void av_file_free(av_file *file) {
	file->flags |= AV_FILE_FREED;
	if(file->open_stream_count == 0) {
		// don't free anything until all streams are closed
		if(file->flags & AV_FILE_READ) {
			avformat_close_input(&file->format_cxt);
		}
		efree(file->streams);
		efree(file);
	}
}

static void av_stream_free(av_stream *strm) {
	av_file *file = strm->file;
	if(file->open_stream_count) {
		efree(strm->packet_queue);
		avcodec_close(strm->codec_cxt);

		memset(&file->streams[strm->index], 0, sizeof(av_stream));
		file->open_stream_count--;
		if(file->open_stream_count == 0) {
			if(file->flags & AV_FILE_WRITE) {
				av_write_trailer(file->format_cxt);
			}
			if(file->flags & AV_FILE_FREED) {
				// free the file if no zval is referencing it
				av_file_free(file);
			}
		}
	}
}

static void php_free_av_file(zend_rsrc_list_entry *rsrc TSRMLS_DC) {
	av_file_free(rsrc->ptr);
}

static void php_free_av_stream(zend_rsrc_list_entry *rsrc TSRMLS_DC) {
	av_stream_free(rsrc->ptr);
}

/* {{{ PHP_MINIT_FUNCTION
 */
PHP_MINIT_FUNCTION(av)
{
	/* If you have INI entries, uncomment these lines 
	REGISTER_INI_ENTRIES();
	*/
	av_register_all();
	avcodec_register_all();

	le_av_file = zend_register_list_destructors_ex(php_free_av_file, NULL, "av file", module_number);
	le_av_strm = zend_register_list_destructors_ex(php_free_av_stream, NULL, "av stream", module_number);

	return SUCCESS;
}
/* }}} */

/* {{{ PHP_MSHUTDOWN_FUNCTION
 */
PHP_MSHUTDOWN_FUNCTION(av)
{
	/* uncomment this line if you have INI entries
	UNREGISTER_INI_ENTRIES();
	*/
	return SUCCESS;
}
/* }}} */

/* Remove if there's nothing to do at request start */
/* {{{ PHP_RINIT_FUNCTION
 */
PHP_RINIT_FUNCTION(av)
{
	if(le_gd == -1) {
		le_gd = zend_fetch_list_dtor_id("gd");
	}
	return SUCCESS;
}
/* }}} */

/* Remove if there's nothing to do at request end */
/* {{{ PHP_RSHUTDOWN_FUNCTION
 */
PHP_RSHUTDOWN_FUNCTION(av)
{
	if(AV_G(video_buffer)) {
		efree(AV_G(video_buffer));
	}
	return SUCCESS;
}
/* }}} */

/* {{{ PHP_MINFO_FUNCTION
 */
PHP_MINFO_FUNCTION(av)
{
    AVOutputFormat *ofmt = NULL;
    AVInputFormat *ifmt = NULL;
    AVCodec *codec = NULL;

	php_info_print_table_start();
	php_info_print_table_header(2, "av support", "enabled");
	php_info_print_table_end();

	php_info_print_table_start();
	php_info_print_table_colspan_header(4, "Output formats");
	php_info_print_table_header(4, "Name", "Short name", "MIME type", "Extensions");
    while((ofmt = av_oformat_next(ofmt))) {
    	php_info_print_table_row(4, ofmt->long_name, ofmt->name, ofmt->mime_type, ofmt->extensions);
    }
	php_info_print_table_colspan_header(3, "Input formats");
	php_info_print_table_header(3, "Name", "Short name", "Extensions");
    while((ifmt = av_iformat_next(ifmt))) {
    	php_info_print_table_row(3, ifmt->long_name, ifmt->name, ifmt->extensions);
    }
	php_info_print_table_colspan_header(2, "Codecs");
	php_info_print_table_header(2, "Name", "Short name");
    while((codec = av_codec_next(codec))) {
    	php_info_print_table_row(2, codec->long_name, codec->name);
    }

	php_info_print_table_end();

	/* Remove comments if you have entries in php.ini
	DISPLAY_INI_ENTRIES();
	*/
}
/* }}} */

/* Every user-visible function in PHP should document itself in the source */
/* {{{ proto string av_file_open(string arg, string mode [, array options])
   Create an encoder */
PHP_FUNCTION(av_file_open)
{
	char *filename, *mode;
	int filename_len, mode_len;
	zval *options = NULL;
	char *code;
	int32_t flags = 0;
	av_file *file;
	AVInputFormat *input_format = NULL;
	AVOutputFormat *output_format = NULL;
	AVFormatContext *format_cxt = NULL;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "ss|a", &filename, &filename_len, &mode, &mode_len, &options) == FAILURE) {
		return;
	}

	for(code = mode; *code; code++) {
		if(*code == 'r') {
			flags |= AV_FILE_READ;
		} else if(*code == 'w') {
			flags |= AV_FILE_WRITE;
		} else if(*code == 'a') {
			flags |= AV_FILE_READ | AV_FILE_WRITE | AV_FILE_APPEND;
		}
	}

	if(!(flags & AV_FILE_READ) && (flags & AV_FILE_WRITE)) {
		AVIOContext *pb = NULL;
		if(options) {
			zval **p_value = NULL;
			if(zend_hash_find(Z_ARRVAL_P(options), "format", 7, (void **) &p_value) != FAILURE) {
				if(Z_TYPE_PP(p_value) == IS_STRING) {
					char *short_name = Z_STRVAL_PP(p_value);
					output_format = av_guess_format(short_name, NULL, NULL);
					if(!output_format) {
						php_error_docref(NULL TSRMLS_CC, E_ERROR, "Cannot find output format: %s", short_name);
						return;
					}
				}
			}
			if(!output_format) {
				if(zend_hash_find(Z_ARRVAL_P(options), "mime", 5, (void **) &p_value) != FAILURE) {
					if(Z_TYPE_PP(p_value) == IS_STRING) {
						char *mime_type = Z_STRVAL_PP(p_value);
						output_format = av_guess_format(NULL, NULL, mime_type);
					}
				}
			}
		}
		if(!output_format) {
			output_format = av_guess_format(NULL, filename, NULL);
			if(!output_format) {
				php_error_docref(NULL TSRMLS_CC, E_ERROR, "Cannot deduce output format from filename: %s", filename);
				return;
			}
		}

		if(!(output_format->flags & AVFMT_NOFILE)) {
			if(avio_open(&pb, filename, AVIO_FLAG_WRITE) < 0) {
				php_error_docref(NULL TSRMLS_CC, E_ERROR, "Error opening file for writing: %s", filename);
				return;
			}
		}
		format_cxt = avformat_alloc_context();
		format_cxt->pb = pb;
		format_cxt->oformat = output_format;
	} else {
		if (avformat_open_input(&format_cxt, filename, NULL, NULL) < 0) {
			php_error_docref(NULL TSRMLS_CC, E_ERROR, "Error opening file for reading: %s", filename);
			return;
		}
		if (avformat_find_stream_info(format_cxt, NULL) < 0) {
			php_error_docref(NULL TSRMLS_CC, E_ERROR, "Error finding stream info: %s", filename);
			return;
		}
		input_format = format_cxt->iformat;
	}

	file = emalloc(sizeof(av_file));
	memset(file, 0, sizeof(av_file));
	file->format_cxt = format_cxt;
	file->input_format = input_format;
	file->output_format = output_format;
	file->flags = flags;

	if(format_cxt->nb_streams) {
		file->streams = emalloc(sizeof(av_stream) * format_cxt->nb_streams);
		file->stream_count = format_cxt->nb_streams;
		memset(file->streams, 0, sizeof(av_stream) * format_cxt->nb_streams);
	}

	ZEND_REGISTER_RESOURCE(return_value, file, le_av_file);
}
/* }}} */

static double av_get_option_double(zval *options, const char *key, double default_value) {
	if(options) {
		if(Z_TYPE_P(options) == IS_ARRAY) {
			zval **p_data;
			if(zend_hash_find(Z_ARRVAL_P(options), key, strlen(key) + 1, (void **) &p_data) == SUCCESS) {
				convert_to_double(*p_data);
				return Z_DVAL_PP(p_data);
			}
		}
	}
	return default_value;
}

static long av_get_option_long(zval *options, const char *key, long default_value) {
	if(options) {
		if(Z_TYPE_P(options) == IS_ARRAY) {
			zval **p_data;
			if(zend_hash_find(Z_ARRVAL_P(options), key, strlen(key) + 1, (void **) &p_data) == SUCCESS) {
				convert_to_long(*p_data);
				return Z_LVAL_PP(p_data);
			}
		}
	}
	return default_value;
}

static const char * av_get_option_string(zval *options, const char *key, const char *default_value) {
	if(options) {
		if(Z_TYPE_P(options) == IS_ARRAY) {
			zval **p_data;
			if(zend_hash_find(Z_ARRVAL_P(options), key, strlen(key) + 1, (void **) &p_data) == SUCCESS) {
				convert_to_string(*p_data);
				return Z_STRVAL_PP(p_data);
			}
		}
	}
	return default_value;
}

static int av_get_stream_type(const char *type) {
	if(strcmp(type, "video") == 0) {
		return AVMEDIA_TYPE_VIDEO;
	} else if(strcmp(type, "audio") == 0) {
		return AVMEDIA_TYPE_AUDIO;
	} else if(strcmp(type, "subtitle") == 0) {
		return AVMEDIA_TYPE_SUBTITLE;
	} else {
		php_error_docref(NULL TSRMLS_CC, E_ERROR, "'%s' is not a recognized stream type", type);
		return -1;
	}
}

/* {{{ proto string av_stream_open(resource file, mixed id, [, array options])
   Create an encoder */
PHP_FUNCTION(av_stream_open)
{
	zval *res, *id, *options = NULL;
	AVCodec *codec = NULL;
	AVCodecContext *codec_cxt = NULL;
	AVStream *stream = NULL;
	AVFrame *frame = NULL;
	av_file *file;
	av_stream *strm;
	int32_t stream_index;
	double time_unit, duration;

	if(zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rz|a", &res, &id, &options) == FAILURE) {
		return;
	}

	ZEND_FETCH_RESOURCE(file, av_file *, &res, -1, "av file", le_av_file);

	if(file->flags & AV_FILE_READ) {
		if(Z_TYPE_P(id) == IS_STRING) {
			int32_t type = av_get_stream_type(Z_STRVAL_P(id));
			if(type < 0) {
				return;
			}
			stream_index = av_find_best_stream(file->format_cxt, type, -1, -1, &codec, 0);
			if(stream_index < 0) {
				php_error_docref(NULL TSRMLS_CC, E_ERROR, "Cannot find a stream of type '%s'", Z_STRVAL_P(id));
				return;
			}
		} else if(Z_TYPE_P(id) == IS_LONG || Z_TYPE_P(id) == IS_DOUBLE) {
			stream_index = (Z_TYPE_P(id) == IS_DOUBLE) ? Z_DVAL_P(id) : Z_LVAL_P(id);
			if(!(stream_index >= 0 && stream_index < file->stream_count)) {
				php_error_docref(NULL TSRMLS_CC, E_ERROR, "Stream index must be between 0 and %d", file->stream_count);
				return;
			}
		} else {
			php_error_docref(NULL TSRMLS_CC, E_ERROR, "parameter 2 should be \"video\", \"audio\", \"subtitle\", or an stream index");
			return;
		}

		stream = file->format_cxt->streams[stream_index];
		codec_cxt = stream->codec;
		if (avcodec_open2(codec_cxt, codec, NULL) < 0) {
			php_error_docref(NULL TSRMLS_CC, E_ERROR, "Unable to open codec '%s'", codec_cxt->codec->name);
			return;
		}
		frame = avcodec_alloc_frame();
		if (codec_cxt->codec->capabilities & CODEC_CAP_TRUNCATED) {
			codec_cxt->flags |= CODEC_FLAG_TRUNCATED;
		}
	} else if(file->flags & AV_FILE_WRITE){
		double frame_rate = av_get_option_double(options, "frame rate", 24.0);
		uint32_t width = av_get_option_long(options, "width", 320);
		uint32_t height = av_get_option_long(options, "height", 240);

		if(Z_TYPE_P(id) == IS_STRING) {
			int32_t type = av_get_stream_type(Z_STRVAL_P(id));
			if(type < 0) {
				return;
			}
		}
		codec = avcodec_find_encoder(file->output_format->video_codec);
		stream = avformat_new_stream(file->format_cxt, codec);
		codec_cxt = stream->codec;
		stream_index = stream->index;

		if(stream->index >= file->stream_count) {
			file->stream_count = stream->index + 1;
			file->streams = erealloc(file->streams, sizeof(av_stream) * file->stream_count);
			memset(&file->streams[stream->index], 0, sizeof(av_stream));
		}

		codec_cxt->width = width;
		codec_cxt->height = height;
		codec_cxt->time_base = av_d2q(1 / frame_rate, 255);
		codec_cxt->pix_fmt = codec->pix_fmts[0];
		codec_cxt->gop_size = 25;

		if (avcodec_open2(codec_cxt, codec, NULL) < 0) {
			php_error_docref(NULL TSRMLS_CC, E_ERROR, "Unable to open codec '%s'", codec_cxt->codec->name);
			return;
		}
		frame = codec_cxt->coded_frame;
		if(codec->type == AVMEDIA_TYPE_VIDEO) {
			avpicture_alloc((AVPicture *) frame, codec_cxt->pix_fmt, codec_cxt->width, codec_cxt->height);
			file->format_cxt->video_codec_id = codec->id;
		} else if(codec->type == AVMEDIA_TYPE_AUDIO) {
			file->format_cxt->audio_codec_id = codec->id;
		} else if(codec->type == AVMEDIA_TYPE_SUBTITLE) {
			file->format_cxt->subtitle_codec_id = codec->id;
		}
	    if(file->output_format->flags & AVFMT_GLOBALHEADER) {
	    	codec_cxt->flags |= CODEC_FLAG_GLOBAL_HEADER;
	    }
	}
	if(codec_cxt->frame_size > 0) {
		time_unit = (double) codec_cxt->frame_size * codec_cxt->time_base.num / codec_cxt->time_base.den;
	} else {
		time_unit = (double) codec_cxt->time_base.num / codec_cxt->time_base.den;
	}
	duration = stream->duration * time_unit;

	file->open_stream_count++;
	strm = &file->streams[stream_index];
	strm->stream = stream;
	strm->codec_cxt = codec_cxt;
	strm->codec = codec_cxt->codec;
	strm->duration = duration;
	strm->time_unit = time_unit;
	strm->packet_queue_size = 16;
	strm->packet_queue = emalloc(sizeof(AVPacket) * strm->packet_queue_size);
	memset(strm->packet_queue, 0, sizeof(AVPacket) * strm->packet_queue_size);
	strm->file = file;
	strm->frame = frame;
	strm->index = stream_index;
	ZEND_REGISTER_RESOURCE(return_value, strm, le_av_strm);
}
/* }}} */

static void av_unload_current_packet(av_stream *strm) {
	av_free_packet(strm->packet);
	memset(strm->packet, 0, sizeof(AVPacket));
	if(strm->packet_count > 1) {
		strm->packet_count--;
		strm->packet_queue_head++;
		if(strm->packet_queue_head >= strm->packet_queue_size) {
			strm->packet_queue_head = 0;
		}
	} else {
		strm->packet_count = 0;
		strm->packet_queue_head = strm->packet_queue_tail = 0;
	}
	strm->packet = NULL;
}

static int av_load_next_packet(av_stream *strm) {
	if(strm->packet) {
		av_unload_current_packet(strm);
	}
	if(strm->packet_count == 0) {
		av_file *file = strm->file;
		av_stream *dst_strm = NULL;
		AVPacket packet;
		do {
			if(av_read_frame(file->format_cxt, &packet) < 0) {
				break;
			}
			if(packet.stream_index >= 0 && packet.stream_index < file->stream_count) {
				dst_strm = &file->streams[packet.stream_index];
			} else {
				php_error_docref(NULL TSRMLS_CC, E_NOTICE, "Invalid stream index: %d", packet.stream_index);
				dst_strm = NULL;
			}
			if(dst_strm && dst_strm->packet_count < dst_strm->packet_queue_size) {
				dst_strm->packet_queue[strm->packet_queue_tail] = packet;
				dst_strm->packet_count++;
				dst_strm->packet_queue_tail++;
				if(dst_strm->packet_queue_tail >= dst_strm->packet_queue_size) {
					dst_strm->packet_queue_tail = 0;
				}
			} else {
				if(dst_strm->packet_queue_size > 0) {
					php_error_docref(NULL TSRMLS_CC, E_WARNING, "Dropping packet for stream %d", dst_strm->index);
				}
				av_free_packet(&packet);
			}
		} while(dst_strm != strm);
	}
	if(strm->packet_count > 0) {
		// point it to the packet at the head of the queue
		strm->packet = &strm->packet_queue[strm->packet_queue_head];
		strm->packet_bytes_remaining = strm->packet->size;
		return TRUE;
	} else {
		strm->packet = NULL;
		return FALSE;
	}
}

static int av_copy_from_gd_image(av_stream *strm, gdImagePtr image) {
	uint32_t i, j;
	int *gd_pixel;
	uint8_t *av_pixel;

	if(!strm->picture || strm->picture->width != image->sx || strm->picture->height != image->sy) {
		if(strm->picture) {
			avpicture_free((AVPicture *) strm->picture);
		} else {
			strm->picture = avcodec_alloc_frame();
		}
		avpicture_alloc((AVPicture *) strm->picture, PIX_FMT_RGBA, image->sx, image->sy);
		strm->picture->width = image->sx;
		strm->picture->height = image->sy;
		strm->scalar_cxt = sws_getCachedContext(strm->scalar_cxt, image->sx, image->sy, PIX_FMT_RGBA, strm->codec_cxt->width, strm->codec_cxt->height, strm->codec_cxt->pix_fmt, SWS_FAST_BILINEAR, NULL, NULL, NULL);
	}

	for(i = 0; i < image->sy; i++) {
		gd_pixel = image->tpixels[i];
		av_pixel = strm->picture->data[0] + strm->picture->linesize[0] * i;
		for(j = 0; j < image->sx; j++) {
			av_pixel[0] = gdTrueColorGetRed(*gd_pixel);
			av_pixel[1] = gdTrueColorGetGreen(*gd_pixel);
			av_pixel[2] = gdTrueColorGetBlue(*gd_pixel);
			av_pixel[3] = (gdAlphaMax - gdTrueColorGetAlpha(*gd_pixel)) << 1;
			gd_pixel += 1;
			av_pixel += 4;
		}
	}

	sws_scale(strm->scalar_cxt, strm->picture->data, strm->picture->linesize, 0, strm->picture->height, strm->frame->data, strm->frame->linesize);
	return TRUE;
}

int avcodec_encode_video2(AVCodecContext *avctx, AVPacket *avpkt, const AVFrame *frame, int *got_packet_ptr) {
	int ret;
	TSRMLS_FETCH();

	if(!AV_G(video_buffer)) {
		AV_G(video_buffer_size) = 100000;
		AV_G(video_buffer) = emalloc(AV_G(video_buffer_size));
	}
	ret = avcodec_encode_video(avctx, AV_G(video_buffer), AV_G(video_buffer_size), frame);

	if(ret > 0) {
		avpkt->size = ret;
		avpkt->data = av_realloc(avpkt->data, avpkt->size + FF_INPUT_BUFFER_PADDING_SIZE);
		memcpy(avpkt->data, AV_G(video_buffer), ret);
		avpkt->pts = avpkt->dts = frame->pts;
		*got_packet_ptr = TRUE;
	}
	return ret;
}

static int av_encode_next_frame(av_stream *strm) {
	av_file *file = strm->file;
	int packet_finished;
	int bytes_encoded;

	if(!(file->flags & AV_FILE_HEADER_WRITTEN)) {
		avformat_write_header(file->format_cxt, NULL);
		file->flags |= AV_FILE_HEADER_WRITTEN;
	}

	strm->frame->pts++;
	strm->packet = &strm->packet_queue[strm->packet_queue_head];
	av_init_packet(strm->packet);

	switch(strm->codec->type) {
		case AVMEDIA_TYPE_VIDEO:
			bytes_encoded = avcodec_encode_video2(strm->codec_cxt, strm->packet, strm->frame, &packet_finished);
			break;
		case AVMEDIA_TYPE_AUDIO:
			bytes_encoded = avcodec_encode_audio2(strm->codec_cxt, strm->packet, strm->frame, &packet_finished);
			break;
		case AVMEDIA_TYPE_SUBTITLE:
			break;
		default:
			break;
	}

	if(bytes_encoded < 0) {
		return FALSE;
	} else if(bytes_encoded > 0) {
		if(strm->frame->pts != AV_NOPTS_VALUE) {
			strm->packet->pts= av_rescale_q(strm->frame->pts, strm->codec_cxt->time_base, strm->stream->time_base);
		}
		if(strm->codec_cxt->coded_frame->key_frame) {
			strm->packet->flags |= AV_PKT_FLAG_KEY;
		}
		strm->packet->stream_index = strm->stream->index;
		if(av_interleaved_write_frame(file->format_cxt, strm->packet) < 0) {
			return FALSE;
		}
	}
	return TRUE;
}

/* {{{ proto bool av_stream_write_image()
    */
PHP_FUNCTION(av_stream_write_image)
{
	zval *res1, *res2;
	av_stream *strm;
	gdImagePtr image;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rr", &res1, &res2) == FAILURE) {
		return;
	}

	ZEND_FETCH_RESOURCE(strm, av_stream *, &res1, -1, "av stream", le_av_strm);
	ZEND_FETCH_RESOURCE(image, gdImagePtr, &res2, -1, "image", le_gd);

	av_copy_from_gd_image(strm, image);
	av_encode_next_frame(strm);
}
/* }}} */

static int av_decode_next_frame(av_stream *strm) {
	int frame_finished = FALSE;
	while(!frame_finished) {
		int bytes_decoded;
		if(strm->packet_bytes_remaining == 0) {
			if(!av_load_next_packet(strm)) {
				return FALSE;
			}
		}
		switch(strm->codec->type) {
			case AVMEDIA_TYPE_VIDEO:
				bytes_decoded = avcodec_decode_video2(strm->codec_cxt, strm->frame, &frame_finished, strm->packet);
				break;
			case AVMEDIA_TYPE_AUDIO:
				bytes_decoded = avcodec_decode_audio4(strm->codec_cxt, strm->frame, &frame_finished, strm->packet);
				break;
			case AVMEDIA_TYPE_SUBTITLE:
				//bytes_decoded = avcodec_decode_subtitle2(strm->codec_cxt, dec->frame, &audio_frame_finished, strm->packet);
				break;
			default:
				bytes_decoded = -1;
		}
		if(bytes_decoded < 0) {
			return FALSE;
		}
		strm->packet_bytes_remaining -= bytes_decoded;
	}
	strm->frame_time = strm->codec_cxt->frame_number * strm->time_unit;
	if(!strm->packet_bytes_remaining) {
		av_unload_current_packet(strm);
	}
	return TRUE;
}

static int av_copy_to_gd_image(av_stream *strm, gdImagePtr image) {
	uint32_t i, j;
	int *gd_pixel;
	uint8_t *av_pixel;

	if(!strm->picture || strm->picture->width != image->sx || strm->picture->height != image->sy) {
		if(strm->picture) {
			avpicture_free((AVPicture *) strm->picture);
		} else {
			strm->picture = avcodec_alloc_frame();
		}
		avpicture_alloc((AVPicture *) strm->picture, PIX_FMT_RGBA, image->sx, image->sy);
		strm->picture->width = image->sx;
		strm->picture->height = image->sy;
		strm->scalar_cxt = sws_getCachedContext(strm->scalar_cxt, strm->codec_cxt->width, strm->codec_cxt->height, strm->codec_cxt->pix_fmt, image->sx, image->sy, PIX_FMT_RGBA, SWS_FAST_BILINEAR, NULL, NULL, NULL);
	}
	sws_scale(strm->scalar_cxt, strm->frame->data, strm->frame->linesize, 0, strm->frame->height, strm->picture->data, strm->picture->linesize);

	for(i = 0; i < image->sy; i++) {
		gd_pixel = image->tpixels[i];
		av_pixel = strm->picture->data[0] + strm->picture->linesize[0] * i;
		for(j = 0; j < image->sx; j++) {
			int r = av_pixel[0];
			int g = av_pixel[1];
			int b = av_pixel[2];
			int a = gdAlphaMax - (av_pixel[3] >> 1);
			*gd_pixel = gdTrueColorAlpha(r, g, b, a);
			gd_pixel += 1;
			av_pixel += 4;
		}
	}
	return TRUE;
}

/* {{{ proto string av_stream_read_image()
   Extract a video frame */
PHP_FUNCTION(av_stream_read_image)
{
	zval *res1, *res2;
	av_stream *strm;
	gdImagePtr image;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "rr", &res1, &res2) == FAILURE) {
		return;
	}

	ZEND_FETCH_RESOURCE(strm, av_stream *, &res1, -1, "av stream", le_av_strm);
	ZEND_FETCH_RESOURCE(image, gdImagePtr, &res2, -1, "image", le_gd);

	if(av_decode_next_frame(strm) && av_copy_to_gd_image(strm, image)) {
		RETVAL_TRUE;
	} else {
		RETVAL_FALSE;
	}
}
/* }}} */

/* {{{ proto string av_file_close(resource res)
   Close an av file */
PHP_FUNCTION(av_file_close)
{
	zval *res;
	av_file *file;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r", &res) == FAILURE) {
		return;
	}

	ZEND_FETCH_RESOURCE(file, av_file *, &res, -1, "av file", le_av_file);
	if(file->open_stream_count == 0) {
		RETVAL_TRUE;
	} else {
		RETVAL_FALSE;
	}
	zend_list_delete(Z_LVAL_P(res));
}
/* }}} */

/* {{{ proto string av_stream_close(resource res)
   Close an av stream */
PHP_FUNCTION(av_stream_close)
{
	zval *res;
	av_stream *strm;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "r", &res) == FAILURE) {
		return;
	}

	ZEND_FETCH_RESOURCE(strm, av_stream *, &res, -1, "av stream", le_av_strm);
	RETVAL_TRUE;
	zend_list_delete(Z_LVAL_P(res));
}
/* }}} */

/* The previous line is meant for vim and emacs, so it can correctly fold and 
   unfold functions in source code. See the corresponding marks just before 
   function definition, where the functions purpose is also documented. Please 
   follow this convention for the convenience of others editing your code.
*/


/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
