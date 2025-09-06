# multirun
...is a tiny Linux utility for running multiple programs in parallel, like a (very) stripped-down version of GNU `parallel`.
It has no dependencies aside from libc and produces tiny static build, making it suitable for use in containers or low-resource environments.

## Building
Install `musl-gcc` (on Ubuntu, you will need the `gcc` and `musl-tools` packages) and run [./build.sh](./build.sh). 
This will produce a static binary in `build/multirun`.

Alternatively, compile [multirun.c](./multirun.c) with a C compiler of your choice.

## Usage
The command line for each subprocess is supplied in sequence, prefixed by a command separator of your choice:
```
./multirun [<command separator> <command>]*
```
For example:
```bash
./multirun -- /bin/echo 123 -- /bin/sleep 3 -- /bin/sh -c "sleep 2; echo Hello World!"
```
The command separator should be a string that does not occur in the subprocess command lines. 
If you don't know what the subprocess command lines will be in advance, you can use a long random string:
```bash
./multirun 39c2b4f73caf3e74 $COMMAND_1 39c2b4f73caf3e74 $COMMAND_2 39c2b4f73caf3e74 $COMMAND_3
```

### Behavior when subprocesses fail
`multirun` exits with exit code 0 if (and only if) all subprocesses exited with exit code 0.

By default, if a subprocess exits with a nonzero exit code, `multirun` still waits for all other subprocesses to finish before returning.
Setting the `MULTIRUN_ON_FAILURE` environment variable to `ABORT` changes this behavior - in that case, when a subprocess exits with a nonzero exit code, `multirun` sends a `SIGTERM` signal to all other subprocesses which are still running:

```bash
MULTIRUN_ON_FAILURE=ABORT ./multirun -- /bin/echo "Hello World!" -- /bin/sh -c "sleep 1; exit 1" -- /bin/sh -c "sleep 2; echo You should not see this!"
```

Note that the `SIGTERM` is only sent to the <b>direct</b> children started by `multirun`.
If one of these children started children of its own, they will not receive a `SIGTERM`. 

## Usage in containers
`multirun` reaps zombies and forwards some signals (including `SIGINT`, `SIGTERM`, `SIGHUP`,  `SIGQUIT`, `SIGUSR1`, and `SIGUSR2`) to all direct children.
As such, it should be a (mostly) valid `init` program for containers.

## Should I use this in production?
No. This is a hobby project, and not a particularly battle-tested one. In production, you're probably better off using a combination of `tini` and `supervisord` to achieve the same goals.

On the other hand, if you, like me, like the idea of creating lean setups with minimalistic tooling and want to use something like `multirun` in a hobby project, I'd appreciate the feedback on how well (or how horribly) it performs.

## License
Copyright (c) 2025 Valentin Dimov

This project is licensed under the zero-clause BSD license, see [LICENSE](./LICENSE).
