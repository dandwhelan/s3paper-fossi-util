#ifndef MINIZ_RENAME_H
#define MINIZ_RENAME_H

/* Rename all exported miniz functions to avoid conflicts with platform/M5GFX
 * versions */

/* Core */
#define mz_free epub_mz_free
#define mz_adler32 epub_mz_adler32
#define mz_crc32 epub_mz_crc32
#define mz_version epub_mz_version
#define mz_error epub_mz_error

/* Deflate */
#define mz_deflateInit epub_mz_deflateInit
#define mz_deflateInit2 epub_mz_deflateInit2
#define mz_deflateReset epub_mz_deflateReset
#define mz_deflate epub_mz_deflate
#define mz_deflateEnd epub_mz_deflateEnd
#define mz_deflateBound epub_mz_deflateBound
#define mz_compress epub_mz_compress
#define mz_compress2 epub_mz_compress2
#define mz_compressBound epub_mz_compressBound
#define tdefl_compress_mem_to_heap epub_tdefl_compress_mem_to_heap
#define tdefl_compress_mem_to_mem epub_tdefl_compress_mem_to_mem
#define tdefl_compress_mem_to_output epub_tdefl_compress_mem_to_output
#define tdefl_init epub_tdefl_init
#define tdefl_compress epub_tdefl_compress
#define tdefl_compress_buffer epub_tdefl_compress_buffer
#define tdefl_get_prev_return_status epub_tdefl_get_prev_return_status
#define tdefl_get_adler32 epub_tdefl_get_adler32
#define tdefl_create_comp_flags_from_zip_params                                \
  epub_tdefl_create_comp_flags_from_zip_params
#define tdefl_compressor_alloc epub_tdefl_compressor_alloc
#define tdefl_compressor_free epub_tdefl_compressor_free

/* Inflate */
#define mz_inflateInit epub_mz_inflateInit
#define mz_inflateInit2 epub_mz_inflateInit2
#define mz_inflateReset epub_mz_inflateReset
#define mz_inflate epub_mz_inflate
#define mz_inflateEnd epub_mz_inflateEnd
#define mz_uncompress epub_mz_uncompress
#define mz_uncompress2 epub_mz_uncompress2
#define tinfl_decompress_mem_to_heap epub_tinfl_decompress_mem_to_heap
#define tinfl_decompress_mem_to_mem epub_tinfl_decompress_mem_to_mem
#define tinfl_decompress_mem_to_callback epub_tinfl_decompress_mem_to_callback
#define tinfl_decompressor_alloc epub_tinfl_decompressor_alloc
#define tinfl_decompressor_free epub_tinfl_decompressor_free
#define tinfl_decompress epub_tinfl_decompress

/* Allocation callbacks */
#define miniz_def_alloc_func epub_miniz_def_alloc_func
#define miniz_def_free_func epub_miniz_def_free_func
#define miniz_def_realloc_func epub_miniz_def_realloc_func

/* ZIP Archive */
#define mz_zip_reader_init_file epub_mz_zip_reader_init_file
#define mz_zip_reader_init_mem epub_mz_zip_reader_init_mem
#define mz_zip_reader_init_file_v2 epub_mz_zip_reader_init_file_v2
#define mz_zip_reader_init_mem_v2 epub_mz_zip_reader_init_mem_v2
#define mz_zip_reader_is_file_encrypted epub_mz_zip_reader_is_file_encrypted
#define mz_zip_reader_is_file_supported epub_mz_zip_reader_is_file_supported
#define mz_zip_reader_get_num_files epub_mz_zip_reader_get_num_files
#define mz_zip_reader_get_filename epub_mz_zip_reader_get_filename
#define mz_zip_reader_file_stat epub_mz_zip_reader_file_stat
#define mz_zip_reader_locate_file epub_mz_zip_reader_locate_file
#define mz_zip_reader_locate_file_v2 epub_mz_zip_reader_locate_file_v2
#define mz_zip_reader_extract_to_mem epub_mz_zip_reader_extract_to_mem
#define mz_zip_reader_extract_to_mem_no_alloc                                  \
  epub_mz_zip_reader_extract_to_mem_no_alloc
#define mz_zip_reader_extract_file_to_file                                     \
  epub_mz_zip_reader_extract_file_to_file
#define mz_zip_reader_extract_to_file epub_mz_zip_reader_extract_to_file
#define mz_zip_reader_extract_to_callback epub_mz_zip_reader_extract_to_callback
#define mz_zip_reader_extract_to_heap epub_mz_zip_reader_extract_to_heap
#define mz_zip_reader_end epub_mz_zip_reader_end
#define mz_zip_writer_init_file epub_mz_zip_writer_init_file
#define mz_zip_writer_init_mem epub_mz_zip_writer_init_mem
#define mz_zip_writer_init_file_v2 epub_mz_zip_writer_init_file_v2
#define mz_zip_writer_add_mem epub_mz_zip_writer_add_mem
#define mz_zip_writer_add_mem_ex epub_mz_zip_writer_add_mem_ex
#define mz_zip_writer_add_file epub_mz_zip_writer_add_file
#define mz_zip_writer_add_cfile epub_mz_zip_writer_add_cfile
#define mz_zip_writer_add_from_zip_reader epub_mz_zip_writer_add_from_zip_reader
#define mz_zip_writer_finalize_archive epub_mz_zip_writer_finalize_archive
#define mz_zip_writer_finalize_heap_archive                                    \
  epub_mz_zip_writer_finalize_heap_archive
#define mz_zip_writer_end epub_mz_zip_writer_end
#define mz_zip_add_mem_to_archive_file_in_place                                \
  epub_mz_zip_add_mem_to_archive_file_in_place
#define mz_zip_validate_file_archive epub_mz_zip_validate_file_archive
#define mz_zip_validate_mem_archive epub_mz_zip_validate_mem_archive
#define mz_zip_get_error_string epub_mz_zip_get_error_string

#endif
