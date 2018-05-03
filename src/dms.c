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

#include <dms.h>

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>

#include <curl/curl.h>
#include <jansson.h>

#include <readconf.h>
#include <dms-crud.h>

#define unlikely(x)    __builtin_expect(!!(x), 0)

#define SNITCH_CREATE_TEMPLATE \
  "{\"name\":\"%s daily ClamAV\", \"interval\":\"daily\", \"tags\":[\"production\", \"anti-virus\"]}"

typedef enum {
  COMMISSION,
  DECOMMISSION,
  REPORT
} Action;

Options options;

const char* load_token() {

   static char token[256];
   FILE* file; 
   long size;
   size_t rv;

   if ((file = fopen(TOKEN_FILE, "r")) == NULL) {
      return NULL;
   }

   fseek(file, 0 , SEEK_END);
   size = ftell(file);
   rewind(file);
   rv = fread(token, 1, size, file);
   if (rv != size) {
      fprintf(stderr, "could not read the whole file\n");
      fclose(file);
      return NULL;
   }
   fclose(file);
   token[strcspn(token, "\n")] = '\0';
   return token;
}

int dms_commission(CURL* curl) {

   FILE* file;
   json_t* val;
   json_t* token;
   char req[JSON_BUF_LEN];
   const char* text;

   snprintf(req, JSON_BUF_LEN, SNITCH_CREATE_TEMPLATE, options.system_name);

   val = dms_create(curl, options.api_key, req);

   if ((file = fopen(TOKEN_FILE, "wb")) == NULL) {
      fprintf(stderr, "could not open token file\n");    
      return 1;
   }

   if (!json_is_object(val)) {
      /* error: json root is not an object */
      fprintf(stderr, "json root is not an object\n");
      json_decref(val);
      return 1;
   }

   token = json_object_get(val, "token");
   if (!json_is_string(token)) {
      fprintf(stderr, "token is not a string\n");
      json_decref(val);
      return 1;
   }

   text = json_string_value(token);
   fwrite(text, sizeof(char), strlen(text), file);
   fclose(file);

   json_decref(val);

   return 0;
}

int dms_decommission(CURL* curl) {

   long size;
   size_t rv;
   const char* token;

   if ((token = load_token()) == NULL) {
      fprintf(stderr, "failed to load token\n");
      return 1;
   }

   if (dms_delete(curl, options.api_key, token) != 0) {
      fprintf(stderr, "failed to delete\n");
      return 1;
   }

   if (remove(TOKEN_FILE)) { 
      fprintf(stderr, "failed to remove token file\n");
      return 1;
   }

   return 0;
}

int dms_report(CURL* curl) { 

   const char* token; 

   if ((token = load_token()) == NULL) { 
      fprintf(stderr, "failed to load token\n");
      return 1;
   }

   if (!dms_check_in(curl, token)) {
      fprintf(stderr, "failed to check-in\n");
      return 1;
   }

   return 0;
}

int main(int argc, char* argv[]) {

   int c;
   CURL *curl;
   int action = REPORT;
   int rv; 

   while ((c = getopt(argc, argv, "cd")) != -1) {
      switch (c) {
      case 'c':
         action = COMMISSION;
         break;
      case 'd':
         action = DECOMMISSION; 
         break;
      default:
         action = REPORT;
         break;
      }
   }

   if (curl_global_init(CURL_GLOBAL_ALL)) {
      fprintf(stderr, "CURL global initialization failed\n");
      return 1;
   }

   initialize_options(&options);

   read_config_file(CONF_FILE, &options);

   curl = curl_easy_init();

   if (unlikely(!curl)) {
      fprintf(stderr, "CURL initialization failed\n");
      return 1;
   }

  switch (action) {
  case COMMISSION:
    rv = dms_commission(curl);
    break;
  case DECOMMISSION:
    rv = dms_decommission(curl);
    break;
  case REPORT:
    rv = dms_report(curl);
    break;
  }

  curl_easy_cleanup(curl);
  curl_global_cleanup();

  free_options(&options);

  return rv; 
}


