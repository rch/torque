#include "license_pbs.h" /* See here for the software license */
#include <stdlib.h>
#include <stdio.h> /* fprintf */
#include <limits.h> /* _POSIX_PATH_MAX */

#include "attribute.h" /* attribute_def */
#include "pbs_job.h"
#include "queue.h"

char *acct_file;
char path_acct[_POSIX_PATH_MAX];
attribute_def job_attr_def[10];
int LOGLEVEL=0;
bool cray_enabled;

int log_remove_old(char *DirPath, unsigned long ExpireTime) 
  {
  fprintf(stderr, "The call to log_remove_old needs to be mocked!!\n");
  exit(1);
  }

void delete_link(struct list_link *old)
  {
  fprintf(stderr, "The call to delete_link needs to be mocked!!\n");
  exit(1);
  }

void *get_next(list_link pl, char *file, int line)
  {
  fprintf(stderr, "The call to get_next needs to be mocked!!\n");
  exit(1);
  }

int unlock_queue(pbs_queue *the_queue, const char *id, const char *msg, int logging)
  {
  return(0);
  }

pbs_queue *get_jobs_queue(job **pjob)
  {
  return((*pjob)->ji_qhdr);
  }

int get_svr_attr_l(int index, long *l)
  {
  return(0);
  }

int get_svr_attr_b(int index, bool *b)
  {
  return(0);
  }

void log_err(int errnum, const char *routine, const char *text) {}
void log_record(int eventtype, int objclass, const char *objname, const char *text) {}
void log_event(int eventtype, int objclass, const char *objname, const char *text) {}

int is_whitespace(

  char c)

  {
  if ((c == ' ')  ||
      (c == '\n') ||
      (c == '\t') ||
      (c == '\r') ||
      (c == '\f'))
    return(TRUE);
  else
    return(FALSE);
  } /* END is_whitespace */

int translate_range_string_to_vector(

  const char       *range_string,
  std::vector<int> &indices)

  {
  char *str = strdup(range_string);
  char *ptr = str;
  int   prev;
  int   curr;

  while (*ptr != '\0')
    {
    prev = strtol(ptr, &ptr, 10);
    
    if (*ptr == '-')
      {
      ptr++;
      curr = strtol(ptr, &ptr, 10);

      while (prev <= curr)
        {
        indices.push_back(prev);

        prev++;
        }

      if ((*ptr == ',') ||
          (is_whitespace(*ptr)))
        ptr++;
      }
    else
      {
      indices.push_back(prev);

      if ((*ptr == ',') ||
          (is_whitespace(*ptr)))
        ptr++;
      }
    }

  free(str);
  return(PBSE_NONE);
  } /* END translate_range_string_to_vector() */

int lock_ji_mutex(job *pjob, const char *id, const char *msg, int logging)
  {
  return(0);
  }

int unlock_ji_mutex(job *pjob, const char *id, const char *msg, int logging)
  {
  return(0);
  }

job::job() {}
job::~job() {}
