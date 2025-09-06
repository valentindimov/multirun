#define _GNU_SOURCE

#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>

static const int fwd_signals[] = {
    SIGINT, SIGTERM, SIGHUP,  SIGQUIT, SIGUSR1, SIGUSR2
};
#define NUM_FWD_SIGNALS (sizeof(fwd_signals) / sizeof(int))

#define MAX_CHILD_PIDS 256
static pid_t child_pids[MAX_CHILD_PIDS] = {0};
static size_t num_child_pids = 0;

static int add_child_pid(pid_t pid) {
    if (num_child_pids >= MAX_CHILD_PIDS) {
        return -1;
    } else {
        child_pids[num_child_pids] = pid;
        num_child_pids++;
        return 0;
    }
}

static void remove_child_pid(pid_t pid) {
    for (size_t i = 0; i < num_child_pids && i < MAX_CHILD_PIDS; i++) {
        if (child_pids[i] == pid) {
            // Found a pid matching the description - shift everything after it back by 1 position and decrease the total number.
            for (size_t j = i + 1; j < num_child_pids && i < MAX_CHILD_PIDS; j++) {
                child_pids[j-1] = child_pids[j];
            }
            num_child_pids--;
            // We could break here, but we're not going to (this lets us handle deleting multiple copies of the same child PID in the list)
        }
    }
}

static void forward_signal(int signal, siginfo_t* siginfo, void* ucontext) {
    // Added to shut up the warnings over unused parameters
    (void)(siginfo);
    (void)(ucontext);
    // Forward the signal to all direct children
    for (size_t i = 0; i < num_child_pids && i < MAX_CHILD_PIDS; i++) {
        if (child_pids[i] > 0) {
            kill(child_pids[i], signal);
        }
    }
}

int main(const int argc, char** argv, char* const *envp) {
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

    // Set up signal forwarding
    struct sigaction forward_sigaction = {0};
    forward_sigaction.sa_sigaction = &forward_signal;
    for (size_t i = 0; i < NUM_FWD_SIGNALS; i++) {
        if (sigaction(fwd_signals[i], &forward_sigaction, NULL) != 0) {
            const char errorMsg[] = "sigaction() failed.\n";
            write(2, errorMsg, sizeof(errorMsg) - 1);
            abort();
        }
    }

    // Start all children in sequence
    char** child_argv_pre = argv + 1;
    while (*child_argv_pre) {
        // Count from the element immediately following child_argv_pre until the next NULL or the next separator.
        // Because child_argv_pre is not at the terminating NULL, we will never read past the end of argv like this.
        size_t child_argv_size = 0;
        while (child_argv_pre[child_argv_size + 1] != NULL && strcmp(child_argv_pre[child_argv_size + 1], separator) != 0) {
            child_argv_size++;
        }
        char** child_argv = child_argv_pre + 1;
        // The child process has child_argv_size arguments (excluding the terminating NULL) starting at child_argv.
        const pid_t child_pid = fork();
        if (child_pid < 0) {
            const char errorMsg[] = "fork() failed.\n";
            write(2, errorMsg, sizeof(errorMsg) - 1);
            abort();
        } else if (child_pid == 0) {
            // Child process -> prepare argv, execve, and exit if that fails
            child_argv[child_argv_size] = NULL;
            execve(child_argv[0], child_argv, envp);
            exit(1);
        } else {
            // Parent process -> remember the child PID and continue
            add_child_pid(child_pid);
            // Shift child_argv_pre to the element immediately following the last child arg
            child_argv_pre = child_argv_pre + child_argv_size + 1;
        }
    }

    // Now wait for all children to exit
    // We will return 0 only if all children exit with 0
    int retval = 0;
    pid_t child_pid;
    int child_status;
    while ((child_pid = waitpid(-1, &child_status, 0)) > 0 || errno == EINTR) {
        // Handles the case where we might've gotten interrupted by a signal
        if (child_pid <= 0) { continue; }
        // Since the child has terminated, remove it from our list of children
        remove_child_pid(child_pid);
        if (WIFSIGNALED(child_status) || (WIFEXITED(child_status) && WEXITSTATUS(child_status) != 0)) {
            if (kill_on_failure) {
                // Make sure we don't send the global SIGTERM twice
                kill_on_failure = 0;
                // Send SIGTERM to each of our non-awaited children
                for (size_t i = 0; i < num_child_pids && i < MAX_CHILD_PIDS; i++) {
                    if (child_pids[i] > 0) {
                        kill(child_pids[i], SIGTERM);
                    }
                }
            }
            retval = 1;
        }
    }

    return retval;
}