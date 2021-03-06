// vim:set et ts=3 sw=3:
//  _____ _         _____                                 _       
// |  __ (_)       |  __ \                               | |      
// | |__) | _ __   | |__) |_ _ _   _ _ __ ___   ___ _ __ | |_ ___ 
// |  ___/ | '_ \  |  ___/ _` | | | | '_ ` _ \ / _ \ '_ \| __/ __|
// | |   | | | | | | |  | (_| | |_| | | | | | |  __/ | | | |_\__ \
// |_|   |_|_| |_| |_|   \__,_|\__, |_| |_| |_|\___|_| |_|\__|___/
//                              __/ |                             
//                             |___/                              
// Copyright (C) 2018 Pin Payments
// http://pinpayments.com
// 
// This software is provided 'as-is', without any express or implied
// warranty.  In no event will the authors be held liable for any damages
// arising from the use of this software.
// 
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it
// freely, subject to the following restrictions:
// 
// 1. The origin of this software must not be misrepresented; you must not
//    claim that you wrote the original software. If you use this software
//    in a product, an acknowledgment in the product documentation would be
//    appreciated but is not required.
// 2. Altered source versions must be plainly marked as such, and must not be
//    misrepresented as being the original software.
// 3. This notice may not be removed or altered from any source distribution.

#include <dms-crud.h>

#include <stdint.h>
#include <string.h>
#include <curl/curl.h>
#include <jansson.h>
#include <limits.h>

#include <dms.h>

#define MAX_URL 256
#define MAX_HEADER 64
#define CURL_TIMEOUT_SECONDS 30

struct download_buffer {
   void*   buf;
   size_t  len;
};

struct upload_buffer {
   const void*  buf;
   size_t		   len;
   size_t		   pos;
};

static size_t download_data_cb(const void* ptr, size_t size, size_t nmemb, void* user_data) {

   struct download_buffer* db = (struct download_buffer*) user_data;
   size_t len = size * nmemb;
   size_t oldlen;
   size_t newlen;
   void* newmem;
   static const unsigned char zero = 0;

   oldlen = db->len;
   newlen = oldlen + len;

   newmem = realloc(db->buf, newlen + 1);
   if (!newmem)
      return 0;

   db->buf = newmem;
   db->len = newlen;
   memcpy((unsigned char*) db->buf + oldlen, ptr, len);
   memcpy((unsigned char*) db->buf + newlen, &zero, 1);	/* null terminate */

   return len;
}

static size_t upload_data_cb(void *ptr, size_t size, size_t nmemb, void *user_data) {

   struct upload_buffer *ub = (struct upload_buffer *) user_data;
   size_t len = size * nmemb;

   if (len > ub->len - ub->pos)
      len = ub->len - ub->pos;

   if (len) {
      memcpy(ptr, ((unsigned char*)ub->buf) + ub->pos, len);
      ub->pos += len;
   }

   return len;
}

static int seek_data_cb(void *user_data, curl_off_t offset, int origin) {

   struct upload_buffer *ub = (struct upload_buffer *) user_data;

   switch (origin) {
   case SEEK_SET:
      ub->pos = (size_t) offset;
      break;
   case SEEK_CUR:
      ub->pos += (size_t) offset;
      break;
   case SEEK_END:
      ub->pos = ub->len + (size_t) offset;
      break;
   default:
      return 1; /* CURL_SEEKFUNC_FAIL */
   }

   return 0; /* CURL_SEEKFUNC_OK */
}

static void download_buffer_free(struct download_buffer* db) {
   if (db && db->len) {
      free(db->buf);
      memset(db, 0, sizeof(*db));
   }
}

json_t* dms_crud_create(CURL* curl, const char* pass, const char* req, const int* verbose) {

   char hdr_len[MAX_HEADER];
   struct download_buffer download_data = { 0 };
   struct upload_buffer upload_data;
   struct curl_slist* headers = NULL;
   json_t* val = NULL;
   json_error_t json_err;
   char* res_buf;
   char curl_err_str[CURL_ERROR_SIZE] = { 0 };
   long http_status;
   int rc = 0;

   if (verbose) {
      curl_easy_setopt(curl, CURLOPT_VERBOSE, (*verbose));
   }

   curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1);
   curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);
   curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, download_data_cb);
   curl_easy_setopt(curl, CURLOPT_WRITEDATA, &download_data);
   curl_easy_setopt(curl, CURLOPT_READFUNCTION, upload_data_cb);
   curl_easy_setopt(curl, CURLOPT_READDATA, &upload_data);
   curl_easy_setopt(curl, CURLOPT_SEEKFUNCTION, seek_data_cb);
   curl_easy_setopt(curl, CURLOPT_SEEKDATA, &upload_data);
   curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, CURL_TIMEOUT_SECONDS);
   curl_easy_setopt(curl, CURLOPT_TIMEOUT, CURL_TIMEOUT_SECONDS);
   curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curl_err_str);

   if (pass) {
      curl_easy_setopt(curl, CURLOPT_USERPWD, pass);
      curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
   }

   curl_easy_setopt(curl, CURLOPT_POST, 1);
   curl_easy_setopt(curl, CURLOPT_URL, DMS_API_URL);

   upload_data.buf = req;
   upload_data.len = strlen(req);
   upload_data.pos = 0;

   curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, upload_data.len);

   headers = curl_slist_append(headers, "Content-Type: application/json");
   headers = curl_slist_append(headers, hdr_len);
   headers = curl_slist_append(headers, "User-Agent: dms");

   curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

   rc = curl_easy_perform(curl);
   if (rc) {
      curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);
      if (http_status == 404) {
         fprintf(stderr, "HTTP request failed: %s\n", curl_err_str);
      }
   }

   if (!download_data.buf) { 
      fprintf(stderr, "No data received from DMS\n");
   }

   val = json_loads(download_data.buf, 0, &json_err);

   curl_slist_free_all(headers);
   download_buffer_free(&download_data);

   return val;
}

int dms_crud_check_in(CURL* curl, const char* token, const int* verbose) {

   char check_in_url[MAX_URL];
   long http_status;
   int rv = 0;

   snprintf(check_in_url, MAX_URL, "https://nosnch.in/%s", token);

   if (verbose) {
      curl_easy_setopt(curl, CURLOPT_VERBOSE, (*verbose));
   }
   curl_easy_setopt(curl, CURLOPT_URL, check_in_url);
   curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);
   curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, CURL_TIMEOUT_SECONDS);
   curl_easy_setopt(curl, CURLOPT_TIMEOUT, CURL_TIMEOUT_SECONDS);

   rv = curl_easy_perform(curl);
   curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);
   if (http_status != 202) {
      fprintf(stderr, "Unexpected HTTP status %ld\n", http_status);
      return rv;
   }

   return rv;
}

int dms_crud_delete(CURL* curl, const char* pass, const char* token, const int* verbose) {
 
   char delete_url[MAX_URL]; 
   char curl_err_str[CURL_ERROR_SIZE] = { 0 };
   long http_status;
   int rc = 0;

   snprintf(delete_url, MAX_URL, "%s/%s", DMS_API_URL, token);

   if (verbose) {
      curl_easy_setopt(curl, CURLOPT_VERBOSE, (*verbose));
   }

   if (pass) {
      curl_easy_setopt(curl, CURLOPT_USERPWD, pass);
      curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
   }

   curl_easy_setopt(curl, CURLOPT_URL, delete_url);
   curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
   curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1);
   curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);
   curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, CURL_TIMEOUT_SECONDS);
   curl_easy_setopt(curl, CURLOPT_TIMEOUT, CURL_TIMEOUT_SECONDS);

   rc = curl_easy_perform(curl);
   curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);
   if (rc) {
      if (http_status == 404) {
         fprintf(stderr, "HTTP request failed: %s\n", curl_err_str);
      }
   }

   return rc;
}

int dms_crud_pause(CURL* curl, const char* pass, const char* token, const int* verbose) {

   char pause_url[MAX_URL];
   char curl_err_str[CURL_ERROR_SIZE] = { 0 };
   long http_status;
   int rc = 0;

   snprintf(pause_url, MAX_URL, "%s/%s/pause", DMS_API_URL, token);

   if (verbose) {
      curl_easy_setopt(curl, CURLOPT_VERBOSE, (*verbose));
   }

   if (pass) {
      curl_easy_setopt(curl, CURLOPT_USERPWD, pass);
      curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
   }

   curl_easy_setopt(curl, CURLOPT_POST, 1);
   curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "");
   curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1);
   curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1);
   curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, CURL_TIMEOUT_SECONDS);
   curl_easy_setopt(curl, CURLOPT_TIMEOUT, CURL_TIMEOUT_SECONDS);
   curl_easy_setopt(curl, CURLOPT_URL, pause_url);

   curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, 0L);

   rc = curl_easy_perform(curl);
   curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);
   if (http_status != 204) {
      fprintf(stderr, "Unexpected HTTP status %ld\n", http_status);
   }

   return rc;
}
