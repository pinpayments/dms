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
#include <errno.h>

#include <curl/curl.h>
#include <jansson.h>

#include <readconf.h>
#include <dms-crud.h>
#include <config.h>

#define unlikely(x)    __builtin_expect(!!(x), 0)

#define SNITCH_CREATE_TEMPLATE \
  "{\"name\":\"%s daily ClamAV\", \"interval\":\"daily\", \"tags\":[\"production\", \"anti-virus\"]}"

#define MAX_TOKEN 256

typedef enum {
  COMMISSION,
  DECOMMISSION,
  REPORT,
  PAUSE
} Action;

Options options;

char token_file[PATH_MAX];

const char* load_token() {;

   static char token[MAX_TOKEN];
   FILE* file; 
   long size;
   size_t rv;

   if ((file = fopen(token_file, "r")) == NULL) {
      fprintf(stderr, "failed to load token\n");
      return NULL;
   }

   fseek(file, 0 , SEEK_END);
   size = ftell(file);
   rewind(file);
   if (size > MAX_TOKEN) {
      fprintf(stderr, "token is larger than supported size\n");
      goto error;
   } 
   rv = fread(token, 1, size, file);
   if (rv != size) {
      fprintf(stderr, "could not read the whole file\n");
      goto error;
   }
   fclose(file);
   token[strcspn(token, "\n")] = '\0';
   return token;
error:
   fclose(file);
   return NULL;
}

void print_version() {
   printf(PACKAGE_STRING " " PACKAGE_URL "\n");
}

void print_usage() {
   static char const usage[] = "\
Usage: " PACKAGE_NAME " [OPTIONS]\n\
Options:\n\
   -c    commission a snitch for this system\n\
   -d    decommission snitch\n\
   -r    report on this snitch\n\
   -p    pause snitch\n\
   -v    display version information and exit\n\
   -h    display this help text and exit\n\
";

   printf(usage);
}

int dms_commission(CURL* curl) {

   FILE* file;
   json_t* val;
   json_t* token;
   char req[JSON_BUF_LEN];
   const char* text;

   /* make this call idempotent */
   if (access(token_file, F_OK) == 0) {
      /* file exists and is readable */
      return 0;
   }

   snprintf(req, JSON_BUF_LEN, SNITCH_CREATE_TEMPLATE, options.system_name);

   val = dms_crud_create(curl, options.api_key, req, &options.verbose);

   if ((file = fopen(token_file, "wb")) == NULL) {
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
      return 1;
   }

   if (dms_crud_delete(curl, options.api_key, token, &options.verbose) != 0) {
      fprintf(stderr, "failed to delete\n");
      return 1;
   }

   if (remove(token_file)) {
      fprintf(stderr, "failed to remove token file\n");
      return 1;
   }

   return 0;
}

int dms_report(CURL* curl) { 

   const char* token; 

   if ((token = load_token()) == NULL) { 
      return 1;
   }

   if (dms_crud_check_in(curl, token, &options.verbose)) {
      fprintf(stderr, "failed to check-in\n");
      return 1;
   }

   return 0;
}

int dms_pause(CURL* curl) {

   const char* token;

   if ((token = load_token()) == NULL) {
      return 1;
   }

   if (dms_crud_pause(curl, options.api_key, token, &options.verbose)) {
      fprintf(stderr, "failed to pause\n");
      return 1;
   }

   return 0;
}

int main(int argc, char* argv[]) {

   int c;
   CURL* curl;
   char* env;
   int action = REPORT;
   char conf_file[PATH_MAX];
   int rv; 

   while ((c = getopt(argc, argv, "cdrpvh")) != -1) {
      switch (c) {
      case 'c':
         action = COMMISSION;
         break;
      case 'd':
         action = DECOMMISSION; 
         break;
      case 'r': 
         action = REPORT;
         break;
      case 'p':
         action = PAUSE;
         break;
      case 'v':
         print_version();
         return 0;
      default:
         print_usage();
         return 0; 
      }
   }

   if (curl_global_init(CURL_GLOBAL_ALL)) {
      fprintf(stderr, "CURL global initialization failed\n");
      return 1;
   }

   initialize_options(&options);

   env = getenv("VERBOSE");
   if (env) {
      options.verbose = (int)strtol(env, (char **)NULL, 10);
      if (options.verbose == 0 && errno == EINVAL) {
         options.verbose = 1;
      }
   }

   env = getenv("CONFIG");
   if (env) {
      strncpy(conf_file, env, PATH_MAX);
   } else {
      strncpy(conf_file, CONF_FILE, PATH_MAX);
   }

   env = getenv("TOKEN");
   if (env) {
      strncpy(token_file, env, PATH_MAX);
   } else {
      strncpy(token_file, TOKEN_FILE, PATH_MAX);
   }

   if (read_config_file(conf_file, &options)) {
      return 1;
   }
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
  case PAUSE:
    rv = dms_pause(curl);
    break;
  }

  curl_easy_cleanup(curl);
  curl_global_cleanup();

  free_options(&options);

  return rv; 
}


