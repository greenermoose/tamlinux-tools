/*
 * tam-file-select — fast native file chooser delegating to desktop XDG portal.
 *
 *   tam-file-select [--title <title>] [--multiple] [--directory] [--extensions "<ext ext...>"]
 *
 * Exits 1 when nothing was picked and 2 when the chooser did not open.
 * Ported to C from Python for Tamlinux; links against libtam.
 *
 * Request token convention:
 *   token = "tamlinux%d"
 *
 * Copyright (c) 2026 Fred Horch <fred.horch@gmail.com>
 * Originally ported from omarchy 4.0.4 by David Heinemeier Hansson.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <tam.h>

#define EXIT_PICKED 0
#define EXIT_NOTHING_PICKED 1
#define EXIT_CHOOSER_FAILED 2

int main(int argc, char *argv[]) {
    tam_portal_file_options_t options;
    memset(&options, 0, sizeof(options));
    options.title = "Select file";

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (strcmp(arg, "--title") == 0) {
            if (i + 1 < argc) {
                options.title = argv[++i];
            } else {
                fprintf(stderr, "tam-file-select: option '--title' requires an argument\n");
                return EXIT_CHOOSER_FAILED;
            }
        } else if (strncmp(arg, "--title=", 8) == 0) {
            options.title = arg + 8;
        } else if (strcmp(arg, "--multiple") == 0) {
            options.multiple = true;
        } else if (strcmp(arg, "--directory") == 0) {
            options.directory = true;
        } else if (strcmp(arg, "--extensions") == 0) {
            if (i + 1 < argc) {
                options.extensions = argv[++i];
            } else {
                fprintf(stderr, "tam-file-select: option '--extensions' requires an argument\n");
                return EXIT_CHOOSER_FAILED;
            }
        } else if (strncmp(arg, "--extensions=", 13) == 0) {
            options.extensions = arg + 13;
        } else {
            fprintf(stderr, "tam-file-select: unknown option %s\n", arg);
            return EXIT_CHOOSER_FAILED;
        }
    }

    tam_portal_file_result_t res = tam_portal_file_choose(&options);

    if (res.status == EXIT_PICKED) {
        if (res.paths) {
            for (size_t i = 0; i < res.count; i++) {
                if (res.paths[i]) {
                    printf("%s\n", res.paths[i]);
                }
            }
        }
        tam_portal_file_result_free(&res);
        return EXIT_PICKED;
    } else if (res.status == EXIT_NOTHING_PICKED) {
        tam_portal_file_result_free(&res);
        return EXIT_NOTHING_PICKED;
    } else {
        if (res.error_message) {
            fprintf(stderr, "tam-file-select: %s\n", res.error_message);
        } else {
            fprintf(stderr, "tam-file-select: failed to open file chooser\n");
        }
        tam_portal_file_result_free(&res);
        return EXIT_CHOOSER_FAILED;
    }
}
