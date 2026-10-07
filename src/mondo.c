// ---------------------------------------------------------------------------
//  mondo.c — il mondo esterno, in lockstep (07/10/2026, §3.84). Il perche' e
//  il protocollo sono in mondo.h.
// ---------------------------------------------------------------------------
#include "mondo.h"

#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

// Il guasto si dice UNA volta, col motivo: dopo, ogni domanda torna -1 e
// basta.
static int guasto(VMondo* w, const char* motivo)
{
  if (!w->guasto)
    fprintf(stderr, "runtime error: mondo: %s\n", motivo);
  w->guasto = 1;
  return -1;
}

int mondo_avvia(VMondo* w, const char* cmd, uint64_t hz, char* err, size_t errsz)
{
  int a[2], b[2];                    // a: simulatore -> mondo, b: mondo -> simulatore
  if (pipe(a) != 0 || pipe(b) != 0)
  {
    snprintf(err, errsz, "mondo: pipe: %s", strerror(errno));
    return -1;
  }
  // Un mondo morto non deve uccidere il simulatore alla prossima scrittura:
  // la scrittura fallisce, e il guasto si dice.
  signal(SIGPIPE, SIG_IGN);
  fflush(stdout);                    // il figlio non deve ereditare un buffer pieno
  pid_t pid = fork();
  if (pid < 0)
  {
    snprintf(err, errsz, "mondo: fork: %s", strerror(errno));
    return -1;
  }
  if (pid == 0)
  {
    dup2(a[0], 0);
    dup2(b[1], 1);
    close(a[0]); close(a[1]); close(b[0]); close(b[1]);
    execl("/bin/sh", "sh", "-c", cmd, (char*) NULL);
    _exit(127);
  }
  close(a[0]);
  close(b[1]);
  w->pid   = pid;
  w->verso = fdopen(a[1], "w");
  w->da    = fdopen(b[0], "r");
  w->on    = 1;
  if (!w->verso || !w->da)
  {
    snprintf(err, errsz, "mondo: fdopen: %s", strerror(errno));
    return -1;
  }
  fprintf(w->verso, "CLOCK %llu\n", (unsigned long long) hz);
  if (fflush(w->verso) != 0)
  {
    snprintf(err, errsz, "mondo: non accetta la riga CLOCK (comando: %s)", cmd);
    return -1;
  }
  return 0;
}

int mondo_tx(VMondo* w, uint64_t t, int neg)
{
  if (w->guasto) return -1;
  fprintf(w->verso, "TX %llu %d\n", (unsigned long long) t, neg ? -1 : 1);
  if (fflush(w->verso) != 0) return guasto(w, "e' morto (TX non consegnato)");
  return 0;
}

int mondo_blocco(VMondo* w, uint64_t t1, uint64_t periodo, int n, double* iq)
{
  if (w->guasto) return -1;
  fprintf(w->verso, "BLOCCO %llu %llu %d\n", (unsigned long long) t1,
          (unsigned long long) periodo, n);
  if (fflush(w->verso) != 0) return guasto(w, "e' morto (BLOCCO non consegnato)");

  char riga[256];
  for (int i = 0; i < n; ++i)
  {
    if (!fgets(riga, sizeof riga, w->da))
      return guasto(w, "e' morto a meta' di un blocco");
    char* p = riga;
    char* end = NULL;
    double ci = strtod(p, &end);
    if (end == p) return guasto(w, "risposta senza I");
    p = end;
    double cq = strtod(p, &end);
    if (end == p) return guasto(w, "risposta senza Q");
    while (*end == ' ' || *end == '\t') end++;
    if (*end != '\n' && *end != '\0') return guasto(w, "risposta con qualcosa dopo Q");
    iq[2 * i]     = ci;
    iq[2 * i + 1] = cq;
  }
  return 0;
}

int mondo_chiudi(VMondo* w)
{
  if (!w->on) return 0;
  if (w->verso) fclose(w->verso);    // EOF: il mondo finisce
  if (w->da) fclose(w->da);
  int st = 0;
  if (waitpid(w->pid, &st, 0) < 0) return -1;
  w->on = 0;
  if (!WIFEXITED(st) || WEXITSTATUS(st) != 0)
    return guasto(w, "e' uscito con un errore");
  return w->guasto ? -1 : 0;
}
