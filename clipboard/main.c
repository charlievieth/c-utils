#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "SDL.h"

// #include <SDL2/SDL.h>
// #include <SDL2/SDL_clipboard.h>
// #include "SDL.h"
// #include "SDL_stdinc.h"
// #include "SDL_clipboard.h"
// #include "SDL_error.h"

void usage() {
    printf("clipboard: [get|set] [TEXT]\n");
}

int main(int argc, char const *argv[]) {
    if (argc < 2) {
        usage();
        return 1;
    }
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        const char *err = SDL_GetError();
        if (err) {
            fprintf(stderr, "error: SDL_Init %s\n", err);
        }
        return 1;
    }
    if (strcmp(argv[1], "get") == 0) {
        char *s = SDL_GetClipboardText();
        if (strlen(s) > 0) {
            printf("%s\n", s);
        }
        SDL_free(s);
        SDL_Quit();
        return 0;
    } else if (strcmp(argv[1], "set") == 0) {
        if (argc != 3) {
            fprintf(stderr, "error: invalid number of arguments: %d\n", argc);
            usage();
            goto exit_error;
        }
        if (SDL_SetClipboardText(argv[2]) != 0) {
            const char *err = SDL_GetError();
            if (err) {
                fprintf(stderr, "error: %s\n", err);
            }
            goto exit_error;
        }
    } else {
        fprintf(stderr, "error: invalid argument: %s\n", argv[1]);
        goto exit_error;
    }
    return 0;

exit_error:
    SDL_Quit();
    return 1;
}
