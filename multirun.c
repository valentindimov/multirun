#include <stdlib.h>
#include <alloca.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>

struct child_pid_list_item {
    pid_t pid;
    struct child_pid_list_item *next;
};

int main(const int argc, char* const *argv, char* const *envp) {
    // Holds the separator between subprocess argvs
    char* separator;
    if (argc < 3 || (separator = argv[1]) == NULL || argv[argc] != NULL) {
        const char usageString[] = "Usage: ./multirun <command separator> <command> [<command separator> <command>]*\n";
        write(2, usageString, sizeof(usageString) - 1);
        return 1;
    }
    // An environment variable controls whether or not we kill other children when one of them fails
    char* on_failure_envvar = getenv("MULTIRUN_ON_FAILURE");
    int kill_on_failure = on_failure_envvar != NULL && strcmp(on_failure_envvar, "ABORT") == 0;
    // Holds the top of the list of all child processes
    struct child_pid_list_item *child_pids = NULL;

    // Start all children in sequence
    char* const * child_argv_pre = argv + 1;
    while (*child_argv_pre) {
        // Count from the element immediately following child_argv_pre until the next NULL or the next separator.
        // Because child_argv_pre is not at the terminating NULL, we will never read past the end of argv like this.
        size_t child_argv_size = 0;
        while (child_argv_pre[child_argv_size + 1] != NULL && strcmp(child_argv_pre[child_argv_size + 1], separator) != 0) {
            child_argv_size++;
        }
        char* const * child_argv_begin = child_argv_pre + 1;
        // The child process has child_argv_size arguments (excluding the terminating NULL) starting at child_argv_begin.
        const pid_t child_pid = fork();
        if (child_pid < 0) {
            const char errorMsg[] = "fork() failed.\n";
            write(2, errorMsg, sizeof(errorMsg) - 1);
            abort();
        } else if (child_pid == 0) {
            // Child process -> prepare argv, execve, and exit if that fails
            char** child_argv = alloca((child_argv_size + 1) * sizeof(char*));
            memcpy(child_argv, child_argv_begin, child_argv_size * sizeof(char*));
            child_argv[child_argv_size] = NULL;
            execve(child_argv[0], child_argv, envp);
            exit(1);
        } else {
            // Parent process -> remember the child PID and continue
            struct child_pid_list_item *new_child = alloca(sizeof(struct child_pid_list_item));
            new_child->pid = child_pid;
            new_child->next = child_pids;
            child_pids = new_child;
            // Shift child_argv_pre to the element immediately following the last child arg
            child_argv_pre = child_argv_pre + child_argv_size + 1;
        }
    }

    // Now wait for all children to exit
    // We will return 0 only if all children exit with 0
    int retval = 0;
    pid_t child_pid;
    int child_status;
    while ((child_pid = waitpid(-1, &child_status, 0)) > 0) {
        // Since the child has terminated, remove it from our list of children
        struct child_pid_list_item **prev_child_ptr = &child_pids;
        for (struct child_pid_list_item *child = *prev_child_ptr; child != NULL; child = child->next) {
            if (child->pid == child_pid) {
                *prev_child_ptr = child->next;
                break;
            }
            prev_child_ptr = &(child->next);
        }
        if (WIFSIGNALED(child_status) || (WIFEXITED(child_status) && WEXITSTATUS(child_status) != 0)) {
            if (kill_on_failure) {
                // Make sure we don't send SIGTERM twice
                kill_on_failure = 0;
                // Send SIGTERM to each of our non-awaited children
                for (struct child_pid_list_item *child = child_pids; child != NULL; child = child->next) {
                    if (child->pid > 0) {
                        kill(child->pid, SIGTERM);
                    }
                }
            }
            retval = 1;
        }
    }

    return retval;
}