#include <signal.h>
#include <string.h>

int ghostlock_umh_entry(int argc, char **argv);
int ghostlock_shell_umh_entry(int argc, char **argv);
int ghostlock_late_load_client(void);

int main(int argc, char **argv) {
  signal(SIGPIPE, SIG_IGN);
  if (argc >= 2 && strcmp(argv[1], "--umh") == 0) {
    return ghostlock_umh_entry(argc, argv);
  }
  if (argc >= 2 && strcmp(argv[1], "--shell-umh") == 0) {
    return ghostlock_shell_umh_entry(argc, argv);
  }
  if (argc == 2 && strcmp(argv[1], "--late-load") == 0) {
    return ghostlock_late_load_client();
  }
  return 2;
}
