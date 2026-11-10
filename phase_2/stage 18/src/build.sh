#!/bin/bash
set -e
gcc -g -static -o xps main.c lib/vec/vec.c lib/parson/parson.c core/xps_core.c core/xps_loop.c core/xps_pipe.c core/xps_session.c network/xps_upstream.c network/xps_connection.c network/xps_listener.c utils/xps_logger.c utils/xps_utils.c utils/xps_buffer.c utils/xps_cliargs.c disk/xps_file.c disk/xps_mime.c disk/xps_directory.c http/xps_http.c http/xps_http_req.c http/xps_http_res.c config/xps_config.c
