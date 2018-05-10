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

#include <readconf.h>

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <sys/types.h>

#define WHITESPACE " \t\r\n"
#define QUOTE	"\""

typedef enum {
   OPCODE_API_KEY,
   OPCODE_SYSTEM_NAME,
   OPCODE_BAD
} OPCODE_TYPE;

static void lowercase(char* s);
static char* strdelim(char** s);
static int process_config_line(Options* options, char* line);

void initialize_options(Options* options) {
   memset(options, 'X', sizeof(*options));
   options->api_key = NULL;
   options->system_name = NULL;
   options->verbose = 0;
}

void free_options(Options* options) {
   if (options->api_key)
      free(options->api_key);
   if (options->system_name)
      free(options->system_name);
}

int read_config_file(const char* filename, Options* options) {
  
   FILE* f;
   char line[1024];

   if ((f = fopen(filename, "r")) == NULL) {
      fprintf(stderr, "%s is missing or unreadable\n", filename);
      return 1;
   }

  while (fgets(line, sizeof(line), f)) {
    if (strlen(line) == sizeof(line) - 1)
      fprintf(stderr, "line too long\n");
    if (process_config_line(options, line) != 0) 
      fprintf(stderr, "bad configuration\n");
  }
  fclose(f);
  return 0;
}

static void lowercase(char *s)
{
	for (; *s; s++)
		*s = tolower((u_char)*s);
}

static char* strdelim(char **s)
{
	char *old;
	int wspace = 0;

	if (*s == NULL)
		return NULL;

	old = *s;

	*s = strpbrk(*s, WHITESPACE QUOTE "=");
	if (*s == NULL)
		return (old);

	if (*s[0] == '\"') {
		memmove(*s, *s + 1, strlen(*s)); /* move nul too */
		/* Find matching quote */
		if ((*s = strpbrk(*s, QUOTE)) == NULL) {
			return (NULL);		/* no matching quote */
		} else {
			*s[0] = '\0';
			*s += strspn(*s + 1, WHITESPACE) + 1;
			return (old);
		}
	}

	/* Allow only one '=' to be skipped */
	if (*s[0] == '=')
		wspace = 1;
	*s[0] = '\0';

	/* Skip any extra whitespace after first token */
	*s += strspn(*s + 1, WHITESPACE) + 1;
	if (*s[0] == '=' && !wspace)
		*s += strspn(*s + 1, WHITESPACE) + 1;

	return (old);
}

static OPCODE_TYPE parse_token(const char* cp) {
  if (strcmp(cp, "dmsapikey") == 0) 
    return OPCODE_API_KEY;
  if (strcmp(cp, "systemname") == 0)
    return OPCODE_SYSTEM_NAME;
  return OPCODE_BAD;
}

static int process_config_line(Options* options, char* line) {

  char* keyword;
  char* arg;
  char* s;
  size_t len;
  OPCODE_TYPE opcode;

  s = line;

  /* Strip trailing whitepace */
  if ((len = strlen(line)) == 0) 
    return 0;

  if ((keyword = strdelim(&s)) == NULL)
    return 0;

  /* we match on lowercase */
  lowercase(keyword);

  opcode = parse_token(keyword); 

  switch (opcode) {
  case OPCODE_API_KEY:
    arg = strdelim(&s);
    if (!arg || *arg == '\0')
      printf("missing api key");
    options->api_key = strdup(arg);
    break;
  case OPCODE_SYSTEM_NAME:
    arg = strdelim(&s);
    if (!arg || *arg == '\0')
      printf("missing system name");
    options->system_name = strdup(arg);
    break;
  case OPCODE_BAD:
    printf("bad configuration directive");
    break;
  }
  return 0;
}
