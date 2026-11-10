#include "../xps.h"
#include <dirent.h>
#include <time.h>

static void html_escape(FILE *out, const char *s) {
  for (; *s; s++) {
    switch (*s) {
    case '&': fputs("&amp;", out); break;
    case '<': fputs("&lt;", out); break;
    case '>': fputs("&gt;", out); break;
    case '"': fputs("&quot;", out); break;
    default: fputc(*s, out);
    }
  }
}

static int cmp_names(const void *a, const void *b) {
  return strcmp(*(char *const *)a, *(char *const *)b);
}

xps_buffer_t *xps_directory_browsing(const char *dir_path, const char *pathname) {
  assert(dir_path != NULL);
  assert(pathname != NULL);

  DIR *dir = opendir(dir_path);
  if (dir == NULL) {
    logger(LOG_ERROR, "xps_directory_browsing()", "opendir() failed");
    return NULL;
  }

  // collect and sort entry names
  vec_void_t names;
  vec_init(&names);
  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
      continue;
    char *name = str_create(entry->d_name);
    if (name == NULL || vec_push(&names, name) != 0) {
      free(name);
      goto fail_names;
    }
  }
  closedir(dir);
  dir = NULL;
  qsort(names.data, names.length, sizeof(char *), cmp_names);

  // build the html in a growing memory stream
  char *html = NULL;
  size_t html_len = 0;
  FILE *out = open_memstream(&html, &html_len);
  if (out == NULL) {
    logger(LOG_ERROR, "xps_directory_browsing()", "open_memstream() failed");
    goto fail_names;
  }

  size_t plen = strlen(pathname);
  const char *sep = (plen > 0 && pathname[plen - 1] == '/') ? "" : "/";

  fputs("<!DOCTYPE html>\n<html>\n<head>\n<meta charset=\"UTF-8\">\n<title>Index of ", out);
  html_escape(out, pathname);
  fputs("</title>\n<style>body{font-family:monospace;margin:2em}"
        "table{border-collapse:collapse}td,th{padding:4px 24px 4px 0;text-align:left}"
        "</style>\n</head>\n<body>\n<h1>Index of ", out);
  html_escape(out, pathname);
  fputs("</h1>\n<hr>\n<table>\n<tr><th>Name</th><th>Last modified</th><th>Size</th></tr>\n", out);

  for (int i = 0; i < names.length; i++) {
    const char *name = names.data[i];
    char *full_path = path_join(dir_path, name);
    if (full_path == NULL) continue;

    struct stat file_stat;
    int rc = stat(full_path, &file_stat);
    free(full_path);
    if (rc != 0) continue; // e.g. broken symlink

    bool is_directory = S_ISDIR(file_stat.st_mode);

    char time_buf[32] = "-";
    struct tm tm_buf;
    if (localtime_r(&file_stat.st_mtime, &tm_buf) != NULL)
      strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &tm_buf);

        fputs("<tr><td><a href=\"", out);
    html_escape(out, pathname);
    fputs(sep, out);
    html_escape(out, name);
    fputs("\">", out);
    html_escape(out, name);
    if (is_directory) fputc('/', out);
    fprintf(out, "</a></td><td>%s</td><td>", time_buf);
    if (is_directory) fputs("-", out);
    else fprintf(out, "%.2f KB", file_stat.st_size / 1024.0);
    fputs("</td></tr>\n", out);
  }

  fputs("</table>\n<hr>\n</body>\n</html>\n", out);
  fclose(out);

  for (int i = 0; i < names.length; i++) free(names.data[i]);
  vec_deinit(&names);

  xps_buffer_t *buff = xps_buffer_create(html_len > 0 ? html_len : 1, html_len, (u_char *)html);
  if (buff == NULL) {
    free(html);
    return NULL;
  }
  return buff;

fail_names:
  if (dir != NULL) closedir(dir);
  for (int i = 0; i < names.length; i++) free(names.data[i]);
  vec_deinit(&names);
  return NULL;
}
