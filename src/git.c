/*
 * Copyright (C) 2009-2013, Gregory P. Ward and contributors.
 *
 * This is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <stdio.h>
#include <errno.h>

#include "git.h"
#include "capture.h"
#include "common.h"
#include <unistd.h>

static int
git_probe(vccontext_t *context)
{
    return isdir(".git") || isfile (".git");
}

static result_t*
git_get_info(vccontext_t *context)
{
    result_t *result = init_result();
    char cwd[1024];
    char buf[1024];

    if (NULL == getcwd (cwd, sizeof(cwd))) {
        debug("error getting current working directory: %s", strerror (errno));
        goto err;        
    }
    
    if (isfile(".git") && read_first_line(".git", buf, 1024)) {
        debug(".git is a regular file, assuming a modern git submodule");
        if (strncmp(buf, "gitdir: ", 8) != 0) {
            debug("modern git submodule .git file does not begin with 'gitdir: '");
            goto err;
        }
        if (strlen(buf) < 9) {
            debug("modern git submodule .git file is blank after 'gitdir: '");
            goto err;
        }
        const char * relative_path = buf + 8;
        if (chdir(relative_path) != 0) {
            debug("unable to chdir to modern git submodule relative path: %s", relative_path);
            goto err;
        }
    } else if (chdir (".git") != 0) {
        debug ("unable to chdir into .git'");
        goto err;
    }

    if (!read_first_line("HEAD", buf, 1024)) {
        debug("unable to read .git/HEAD: assuming not a git repo");
        goto err;
    }

    char *prefix = "ref: refs/heads/";
    int prefixlen = strlen(prefix);

    if (context->options->show_branch || context->options->show_revision) {
        int found_branch = 0;
        if (strncmp(prefix, buf, prefixlen) == 0) {
            /* yep, we're on a known branch */
            debug("read a head ref from .git/HEAD: '%s'", buf);
            if (result_set_branch(result, buf + prefixlen))
                found_branch = 1;
        }
        else {
            /* if it's not a branch name, assume it is a commit ID */
            debug(".git/HEAD doesn't look like a head ref: unknown branch");
            result_set_branch(result, "(unknown)");
            result_set_revision(result, buf, 12);
        }
        if (context->options->show_revision && found_branch) {
            char buf[1024];
            char filename[1024] = "refs/heads/";
            int nchars = sizeof(filename) - strlen(filename) - 1;
            strncat(filename, result->branch, nchars);
            if (read_first_line(filename, buf, 1024)) {
                result_set_revision(result, buf, 12);
            }
        }
    }

    // following stages need to be in the tree, so back out of the .git dir
    chdir(cwd);
    
    if (context->options->show_modified) {
        char *argv[] = {
            "git", "diff", "--no-ext-diff", "--quiet", "--exit-code", NULL};
        capture_t *capture = capture_child("git", argv);
        result->modified = (capture->status == 1);

        /* any other outcome (including failure to fork/exec,
           failure to run git, or diff error): assume no
           modifications */
        free_capture(capture);
    }
    if (context->options->show_unknown) {
        char *argv[] = {
            "git", "ls-files", "--others", "--exclude-standard", NULL};
        capture_t *capture = capture_child("git", argv);
        result->unknown = (capture != NULL && capture->childout.len > 0);

        /* again, ignore other errors and assume no unknown files */
        free_capture(capture);
    }

    return result;

 err:
    free_result(result);
    return NULL;
}

vccontext_t*
get_git_context(options_t *options)
{
    return init_context("git", options, git_probe, git_get_info);
}
