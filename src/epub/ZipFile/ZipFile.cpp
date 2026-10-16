#ifndef UNIT_TEST
#include <esp_log.h>
#else
#define ESP_LOGE(args...)
#define ESP_LOGI(args...)
#endif
#include "ZipFile.h"
#include <Arduino.h>
#include <esp_heap_caps.h>

#include "miniz.h"

#define TAG "ZIP"

// read a file from the zip file allocating the required memory for the data
uint8_t *ZipFile::read_file_to_memory(const char *filename, size_t *size) {
  // open up the epub file using miniz
  // Allocate on PSRAM (SPIRAM) to prevent stack/heap overflow on internal RAM
  mz_zip_archive *zip_archive = (mz_zip_archive *)heap_caps_calloc(
      1, sizeof(mz_zip_archive), MALLOC_CAP_SPIRAM);
  if (!zip_archive) {
    // Fallback to regular malloc if PSRAM fails (unlikely)
    zip_archive = (mz_zip_archive *)calloc(1, sizeof(mz_zip_archive));
  }

  if (!zip_archive) {
    Serial.println("ZIP: Failed to allocate memory for zip_archive");
    return nullptr;
  }

  Serial.printf("ZIP: Opening file %s\n", m_filename.c_str());
  bool status = mz_zip_reader_init_file(zip_archive, m_filename.c_str(), 0);
  if (!status) {
    Serial.printf("ZIP: mz_zip_reader_init_file() failed for %s!\n",
                  m_filename.c_str());
    Serial.printf("ZIP: Error %s\n",
                  mz_zip_get_error_string(zip_archive->m_last_error));
    free(zip_archive); // Cleanup
    return nullptr;
  }

  // find the file
  Serial.printf("ZIP: Locating internal file %s\n", filename);
  mz_uint32 file_index = 0;
  if (!mz_zip_reader_locate_file_v2(zip_archive, filename, nullptr, 0,
                                    &file_index)) {
    Serial.printf("ZIP: Could not find file %s in archive\n", filename);
    mz_zip_reader_end(zip_archive);
    free(zip_archive); // Cleanup
    return nullptr;
  }

  // get the file size - we do this all manually so we can add a null terminator
  // to any strings
  mz_zip_archive_file_stat file_stat;
  if (!mz_zip_reader_file_stat(zip_archive, file_index, &file_stat)) {
    Serial.println("ZIP: mz_zip_reader_file_stat() failed!");
    Serial.printf("ZIP: Error %s\n",
                  mz_zip_get_error_string(zip_archive->m_last_error));
    mz_zip_reader_end(zip_archive);
    free(zip_archive); // Cleanup
    return nullptr;
  }
  // allocate memory for the file
  size_t file_size = file_stat.m_uncomp_size;
  Serial.printf("ZIP: Extracting %s (Size: %u)\n", filename,
                (unsigned int)file_size);
  // Use PSRAM for the file content as well
  uint8_t *file_data =
      (uint8_t *)heap_caps_calloc(file_size + 1, 1, MALLOC_CAP_SPIRAM);
  if (!file_data) {
    // fallback
    Serial.println("ZIP: PSRAM alloc failed, trying RAM");
    file_data = (uint8_t *)calloc(file_size + 1, 1);
  }

  if (!file_data) {
    Serial.printf("ZIP: Failed to allocate memory for %s\n",
                  file_stat.m_filename);
    mz_zip_reader_end(zip_archive);
    free(zip_archive); // Cleanup
    return nullptr;
  }
  // read the file
  status = mz_zip_reader_extract_to_mem(zip_archive, file_index, file_data,
                                        file_size, 0);
  if (!status) {
    ESP_LOGE(TAG, "mz_zip_reader_extract_to_mem() failed!\n");
    ESP_LOGE(TAG, "Error %s\n",
             mz_zip_get_error_string(zip_archive->m_last_error));
    free(file_data);
    mz_zip_reader_end(zip_archive);
    free(zip_archive); // Cleanup
    return nullptr;
  }
  // Close the archive, freeing any resources it was using
  mz_zip_reader_end(zip_archive);
  free(zip_archive); // Cleanup

  // Small delay to ensure file descriptors are fully released
  delay(50);

  // return the size if required
  if (size) {
    *size = file_size;
  }
  return file_data;
}
/*
bool ZipFile::read_file_to_file(const char *filename, const char *dest)
{
  mz_zip_archive zip_archive;
  memset(&zip_archive, 0, sizeof(zip_archive));
  bool status = mz_zip_reader_init_file(&zip_archive, m_filename.c_str(), 0);
  if (!status)
  {
    ESP_LOGE(TAG, "mz_zip_reader_init_file() failed!\n");
    ESP_LOGE(TAG, "Error %s\n",
mz_zip_get_error_string(zip_archive.m_last_error)); return false;
  }
  // Run through the archive and find the requiested file
  for (int i = 0; i < (int)mz_zip_reader_get_num_files(&zip_archive); i++)
  {
    mz_zip_archive_file_stat file_stat;
    if (!mz_zip_reader_file_stat(&zip_archive, i, &file_stat))
    {
      ESP_LOGE(TAG, "mz_zip_reader_file_stat() failed!\n");
      ESP_LOGE(TAG, "Error %s\n",
mz_zip_get_error_string(zip_archive.m_last_error));
      mz_zip_reader_end(&zip_archive);
      return false;
    }
    // is this the file we're looking for?
    if (strcmp(filename, file_stat.m_filename) == 0)
    {
      ESP_LOGI(TAG, "Extracting %s\n", file_stat.m_filename);
      mz_zip_reader_extract_file_to_file(&zip_archive, file_stat.m_filename,
dest, 0); mz_zip_reader_end(&zip_archive); return true;
    }
  }
  mz_zip_reader_end(&zip_archive);
  return false;
}
*/
