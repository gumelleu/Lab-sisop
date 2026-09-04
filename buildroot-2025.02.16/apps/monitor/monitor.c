
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <unistd.h>

/* Structures */

typedef struct {
   unsigned long days;
   unsigned long hours;
   unsigned long minutes;
   unsigned long seconds;
} ProcTime;

#define CPU_SAMPLE_FILE "/tmp/cpu_stats"

typedef struct {
   unsigned long long user;
   unsigned long long nice;
   unsigned long long system;
   unsigned long long idle;
   unsigned long long iowait;
   unsigned long long irq;
   unsigned long long softirq;
   unsigned long long steal;
} CpuStats;

typedef struct {
   unsigned long total_mb;
   unsigned long used_mb;
} MemoryInfo;

typedef struct {
   double one;
   double five;
   double fifteen;
} LoadAverage;

#define PROCESS_NAME_SIZE 256
#define PROCESS_COMMAND_SIZE 4096
#define PROCESS_PATH_SIZE 256

typedef struct {
   unsigned int pid;
   unsigned int ppid;
   unsigned int uid;
   char name[PROCESS_NAME_SIZE];
   char state[64];
   unsigned int threads;
   unsigned long vm_size_kb;
   unsigned long vm_rss_kb;
} ProcessInfo;

typedef struct {
   unsigned long long utime;
   unsigned long long stime;
   long priority;
   long nice;
} ProcessCpuInfo;

/* Utility */

static void format_time(unsigned long seconds, ProcTime *time) {
   time->days = seconds / 86400;
   seconds %= 86400;
   time->hours = seconds / 3600;
   seconds %= 3600;
   time->minutes = seconds / 60;
   time->seconds = seconds % 60;
}

/* Kernel / System version */

static int get_kernel_version(char *version, size_t size) {
   FILE *file = fopen("/proc/sys/kernel/osrelease", "r");

   if (!file)
      return -1;

   if (!fgets(version, size, file)) {
      fclose(file);
      return -1;
   }

   fclose(file);
   version[strcspn(version, "\n")] = '\0';

   return 0;
}

/* Uptime / Idle time */

static int get_uptime(ProcTime *uptime, ProcTime *idle) {
   FILE *file = fopen("/proc/uptime", "r");

   if (!file)
      return -1;

   double uptime_seconds;
   double idle_seconds;

   if (fscanf(file, "%lf %lf", &uptime_seconds, &idle_seconds) != 2) {
      fclose(file);
      return -1;
   }

   fclose(file);

   format_time((unsigned long)uptime_seconds, uptime);
   format_time((unsigned long)idle_seconds, idle);

   return 0;
}

/* RTC */

static int get_rtc(char *date, size_t date_size, char *time, size_t time_size) {
   FILE *file = fopen("/proc/driver/rtc", "r");

   if (!file)
      return -1;

   char line[256];
   int found_date = 0;
   int found_time = 0;

   while (fgets(line, sizeof(line), file)) {
      if (strncmp(line, "rtc_time", 8) == 0) {
         char *colon = strchr(line, ':');

         if (colon) {
            colon++;

            while (*colon == ' ' || *colon == '\t')
               colon++;

            strncpy(time, colon, time_size - 1);
            time[time_size - 1] = '\0';
            time[strcspn(time, "\n")] = '\0';
            found_time = 1;
         }
      } else if (strncmp(line, "rtc_date", 8) == 0) {
         char *colon = strchr(line, ':');

         if (colon) {
            colon++;

            while (*colon == ' ' || *colon == '\t')
               colon++;

            strncpy(date, colon, date_size - 1);
            date[date_size - 1] = '\0';
            date[strcspn(date, "\n")] = '\0';
            found_date = 1;
         }
      }
   }

   fclose(file);

   return (found_date && found_time) ? 0 : -1;
}

/* CPU information */

static int get_cpu_info(char *model, size_t model_size, double *frequency, int *cores) {
   FILE *file = fopen("/proc/cpuinfo", "r");

   if (!file)
      return -1;

   char line[256];

   *frequency = 0.0;
   *cores = 0;
   model[0] = '\0';

   while (fgets(line, sizeof(line), file)) {
      if (strncmp(line, "processor", 9) == 0)
         (*cores)++;

      if (strncmp(line, "model name", 10) == 0) {
         char *colon = strchr(line, ':');

         if (colon) {
            colon++;

            while (*colon == ' ' || *colon == '\t')
               colon++;

            strncpy(model, colon, model_size - 1);
            model[model_size - 1] = '\0';
            model[strcspn(model, "\n")] = '\0';
         }
      } else if (strncmp(line, "Processor", 9) == 0) {
         char *colon = strchr(line, ':');

         if (colon && model[0] == '\0') {
            colon++;

            while (*colon == ' ' || *colon == '\t')
               colon++;

            strncpy(model, colon, model_size - 1);
            model[model_size - 1] = '\0';
            model[strcspn(model, "\n")] = '\0';
         }
      } else if (strncmp(line, "cpu MHz", 7) == 0) {
         char *colon = strchr(line, ':');

         if (colon)
            sscanf(colon + 1, "%lf", frequency);
      }
   }

   fclose(file);

   return 0;
}

/* System load */

static int get_load_average(LoadAverage *load) {
   FILE *file = fopen("/proc/loadavg", "r");

   if (!file)
      return -1;

   if (fscanf(file, "%lf %lf %lf", &load->one, &load->five, &load->fifteen) != 3) {
      fclose(file);
      return -1;
   }

   fclose(file);

   return 0;
}

/* CPU statistics */

static int get_cpu_stats(CpuStats *stats) {
   FILE *file = fopen("/proc/stat", "r");

   if (!file)
      return -1;

   int result = fscanf(file, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
      &stats->user, &stats->nice, &stats->system, &stats->idle,
      &stats->iowait, &stats->irq, &stats->softirq, &stats->steal);

   fclose(file);

   return result == 8 ? 0 : -1;
}

static int save_cpu_stats(const CpuStats *stats) {
   FILE *file = fopen(CPU_SAMPLE_FILE, "w");

   if (!file)
      return -1;

   fprintf(file, "%llu %llu %llu %llu %llu %llu %llu %llu\n",
      stats->user, stats->nice, stats->system, stats->idle,
      stats->iowait, stats->irq, stats->softirq, stats->steal);

   fclose(file);

   return 0;
}

static int load_cpu_stats(CpuStats *stats) {
   FILE *file = fopen(CPU_SAMPLE_FILE, "r");

   if (!file)
      return -1;

   int result = fscanf(file, "%llu %llu %llu %llu %llu %llu %llu %llu",
      &stats->user, &stats->nice, &stats->system, &stats->idle,
      &stats->iowait, &stats->irq, &stats->softirq, &stats->steal);

   fclose(file);

   return result == 8 ? 0 : -1;
}

static int get_cpu_usage(double *usage) {
   CpuStats previous;
   CpuStats current;

   /* Sempre fazemos uma nova leitura de /proc/stat. */
   if (get_cpu_stats(&current) != 0)
      return -1;

   /* Se não existe amostra anterior, esta é a primeira requisição. */
   if (load_cpu_stats(&previous) != 0) {
      /* A amostra atual será usada pela próxima requisição. */
      if (save_cpu_stats(&current) != 0)
         return -1;

      return 1;
   }

   unsigned long long total_previous = previous.user + previous.nice + previous.system + previous.idle + previous.iowait + previous.irq + previous.softirq + previous.steal;
   unsigned long long total_current = current.user + current.nice + current.system + current.idle + current.iowait + current.irq + current.softirq + current.steal;

   unsigned long long busy_previous = total_previous - previous.idle - previous.iowait;
   unsigned long long busy_current = total_current - current.idle - current.iowait;

   unsigned long long total_delta = total_current - total_previous;
   unsigned long long busy_delta = busy_current - busy_previous;

   /* Atualiza a amostra mesmo que não seja possível calcular a porcentagem. */
   if (save_cpu_stats(&current) != 0)
      return -1;

   if (total_delta == 0) {
      *usage = 0.0;
      return 1;
   }

   *usage = 100.0 * (double)busy_delta / (double)total_delta;

   return 0;
}

static double calculate_cpu_usage(const CpuStats *old, const CpuStats *current) {
   unsigned long long old_total = old->user + old->nice + old->system + old->idle + old->iowait + old->irq + old->softirq + old->steal;
   unsigned long long current_total = current->user + current->nice + current->system + current->idle + current->iowait + current->irq + current->softirq + current->steal;

   unsigned long long old_idle = old->idle + old->iowait;
   unsigned long long current_idle = current->idle + current->iowait;

   unsigned long long total_delta = current_total - old_total;
   unsigned long long idle_delta = current_idle - old_idle;

   if (total_delta == 0)
      return 0.0;

   return 100.0 * (double)(total_delta - idle_delta) / (double)total_delta;
}

/* Memory */

static int get_memory(MemoryInfo *memory) {
   FILE *file = fopen("/proc/meminfo", "r");

   if (!file)
      return -1;

   char key[64];
   char unit[16];
   unsigned long value;
   unsigned long mem_total = 0;
   unsigned long mem_available = 0;

   while (fscanf(file, "%63s %lu %15s", key, &value, unit) == 3) {
      if (strcmp(key, "MemTotal:") == 0)
         mem_total = value;
      else if (strcmp(key, "MemAvailable:") == 0)
         mem_available = value;
   }

   fclose(file);

   if (mem_total == 0)
      return -1;

   memory->total_mb = mem_total / 1024;

   if (mem_available <= mem_total)
      memory->used_mb = (mem_total - mem_available) / 1024;
   else
      memory->used_mb = 0;

   return 0;
}

/* Disk I/O */

static void get_disk_stats(void) {
   FILE *file = fopen("/proc/diskstats", "r");

   if (!file)
      return;

   unsigned int major;
   unsigned int minor;
   char device[64];

   unsigned long long reads;
   unsigned long long reads_merged;
   unsigned long long sectors_read;
   unsigned long long read_time;

   unsigned long long writes;
   unsigned long long writes_merged;
   unsigned long long sectors_written;
   unsigned long long write_time;

   while (fscanf(file, "%u %u %63s %llu %llu %llu %llu %llu %llu %llu %llu",
         &major, &minor, device, &reads, &reads_merged, &sectors_read,
         &read_time, &writes, &writes_merged, &sectors_written,
         &write_time) == 11) {
      printf("Device: %s | Reads: %llu | Writes: %llu | Sectors read: %llu | Sectors written: %llu\n",
         device, reads, writes, sectors_read, sectors_written);
   }

   fclose(file);
}

/* Supported filesystems */

static void get_filesystems(void) {
   FILE *file = fopen("/proc/filesystems", "r");

   if (!file)
      return;

   char line[256];

   while (fgets(line, sizeof(line), file)) {
      char type[32];
      char filesystem[64];

      int fields = sscanf(line, "%31s %63s", type, filesystem);

      if (fields == 2) {
         /* "nodev proc": o filesystem name está em `filesystem`. */
         printf("%s\n", filesystem);
      } else if (fields == 1) {
         /* Filesystems with no nodev prefix. */
         printf("%s\n", type);
      }
   }

   fclose(file);
}

/* Character and block devices */

static void get_devices(void) {
   FILE *file = fopen("/proc/devices", "r");

   if (!file)
      return;

   char line[256];
   int section = 0;

   while (fgets(line, sizeof(line), file)) {
      if (strncmp(line, "Character devices:", 18) == 0) {
         section = 1;
         printf("\nCharacter devices:\n");
         continue;
      }

      if (strncmp(line, "Block devices:", 14) == 0) {
         section = 2;
         printf("\nBlock devices:\n");
         continue;
      }

      if (section == 1 || section == 2) {
         unsigned int major;
         char device[128];

         if (sscanf(line, "%u %127s", &major, device) == 2)
            printf("  %3u  %s\n", major, device);
      }
   }

   fclose(file);
}

/* Network devices */

static void get_network_devices(void) {
   FILE *file = fopen("/proc/net/dev", "r");

   if (!file)
      return;

   char line[512];

   while (fgets(line, sizeof(line), file)) {
      char interface[64];

      /* Skip the two header lines. */
      if (!strchr(line, ':'))
         continue;

      if (sscanf(line, " %63[^:]:", interface) == 1)
         printf("%s\n", interface);
   }

   fclose(file);
}

/* Network device statistics */

static void get_network_stats(void) {
   FILE *file = fopen("/proc/net/dev", "r");

   if (!file)
      return;

   char line[512];

   while (fgets(line, sizeof(line), file)) {
      char interface[64];

      unsigned long long rx_bytes;
      unsigned long long rx_packets;
      unsigned long long rx_errors;
      unsigned long long rx_drops;
      unsigned long long rx_fifo;
      unsigned long long rx_frame;
      unsigned long long rx_compressed;
      unsigned long long rx_multicast;

      unsigned long long tx_bytes;
      unsigned long long tx_packets;
      unsigned long long tx_errors;
      unsigned long long tx_drops;
      unsigned long long tx_fifo;
      unsigned long long tx_collisions;
      unsigned long long tx_carrier;
      unsigned long long tx_compressed;

      if (!strchr(line, ':'))
         continue;

      int result = sscanf(line, " %63[^:]: %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
         interface, &rx_bytes, &rx_packets, &rx_errors, &rx_drops,
         &rx_fifo, &rx_frame, &rx_compressed, &rx_multicast,
         &tx_bytes, &tx_packets, &tx_errors, &tx_drops, &tx_fifo,
         &tx_collisions, &tx_carrier, &tx_compressed);

      if (result == 17)
         printf("%s: RX=%llu bytes, TX=%llu bytes, RX packets=%llu, TX packets=%llu\n",
            interface, rx_bytes, tx_bytes, rx_packets, tx_packets);
   }

   fclose(file);
}

/* Processes */

static int is_numeric(const char *str) {
   if (str == NULL || *str == '\0')
      return 0;

   while (*str != '\0') {
      if (!isdigit((unsigned char)*str))
         return 0;

      str++;
   }

   return 1;
}

static int get_requested_pid(char *pid, size_t pid_size) {
   const char *query = getenv("QUERY_STRING");

   if (query == NULL)
      return 0;

   if (strncmp(query, "pid=", 4) != 0)
      return 0;

   const char *value = query + 4;

   if (!is_numeric(value))
      return 0;

   if (strlen(value) >= pid_size)
      return 0;

   strcpy(pid, value);

   return 1;
}

static int get_process_status(const char *pid, ProcessInfo *process) {
   char path[PROCESS_PATH_SIZE];

   snprintf(path, sizeof(path), "/proc/%s/status", pid);

   FILE *file = fopen(path, "r");

   if (!file)
      return -1;

   memset(process, 0, sizeof(*process));
   process->pid = (unsigned int)strtoul(pid, NULL, 10);

   char line[512];

   while (fgets(line, sizeof(line), file)) {
      if (strncmp(line, "Name:", 5) == 0) {
         sscanf(line + 5, " %255[^\n]", process->name);
      } else if (strncmp(line, "State:", 6) == 0) {
         /* Mantém a informação fornecida pelo kernel: S (sleeping), R (running), Z (zombie), etc. */
         sscanf(line + 6, " %63[^\n]", process->state);
      } else if (strncmp(line, "PPid:", 5) == 0) {
         sscanf(line + 5, "%u", &process->ppid);
      } else if (strncmp(line, "Uid:", 4) == 0) {
         sscanf(line + 4, "%u", &process->uid);
      } else if (strncmp(line, "Threads:", 8) == 0) {
         sscanf(line + 8, "%u", &process->threads);
      } else if (strncmp(line, "VmSize:", 7) == 0) {
         sscanf(line + 7, "%lu", &process->vm_size_kb);
      } else if (strncmp(line, "VmRSS:", 6) == 0) {
         sscanf(line + 6, "%lu", &process->vm_rss_kb);
      }
   }

   fclose(file);

   return 0;
}

static int get_process_command_line(const char *pid, char *command, size_t command_size) {
   char path[PROCESS_PATH_SIZE];

   snprintf(path, sizeof(path), "/proc/%s/cmdline", pid);

   FILE *file = fopen(path, "r");

   if (!file)
      return -1;

   size_t length = fread(command, 1, command_size - 1, file);

   fclose(file);

   /* Threads de kernel podem possuir cmdline vazio. */
   if (length == 0) {
      command[0] = '\0';
      return 0;
   }

   command[length] = '\0';

   /* Os argumentos são separados por '\0'. Transformamos os separadores em espaços. */
   for (size_t i = 0; i < length; i++) {
      if (command[i] == '\0')
         command[i] = ' ';
   }

   /* Remove espaços no final. */
   while (length > 0 && command[length - 1] == ' ') {
      command[length - 1] = '\0';
      length--;
   }

   return 0;
}

static int get_process_cpu_info(const char *pid, ProcessCpuInfo *cpu) {
   char path[PROCESS_PATH_SIZE];

   snprintf(path, sizeof(path), "/proc/%s/stat", pid);

   FILE *file = fopen(path, "r");

   if (!file)
      return -1;

   char line[4096];

   if (!fgets(line, sizeof(line), file)) {
      fclose(file);
      return -1;
   }

   fclose(file);

   /* O campo comm pode conter espaços. Portanto, encontramos o último ')' do campo. */
   char *closing_parenthesis = strrchr(line, ')');

   if (!closing_parenthesis)
      return -1;

   char *fields = closing_parenthesis + 2;
   char state;

   int result = sscanf(fields, "%c %*s %*s %*s %*s %*s %*s %*s %*s %*s %*s %llu %llu %*s %*s %ld %ld",
      &state, &cpu->utime, &cpu->stime, &cpu->priority, &cpu->nice);

   return result == 5 ? 0 : -1;
}

static double clock_ticks_to_seconds(unsigned long long ticks) {
   long ticks_per_second = sysconf(_SC_CLK_TCK);

   if (ticks_per_second <= 0)
      return 0.0;

   return (double)ticks / (double)ticks_per_second;
}

static void print_process_list(void) {
   DIR *dir = opendir("/proc");

   if (!dir) {
      printf("<p>Não foi possível acessar /proc.</p>\n");
      return;
   }

   printf("<h2>Processos em execução</h2>\n");
   printf("<ul>\n");

   struct dirent *entry;

   while ((entry = readdir(dir)) != NULL) {
      if (!is_numeric(entry->d_name))
         continue;

      ProcessInfo process;

      if (get_process_status(entry->d_name, &process) != 0)
         continue;

      printf("<li><a href='monitor?pid=%s'>PID %u - %s</a></li>\n",
         entry->d_name, process.pid, process.name);
   }

   printf("</ul>\n");

   closedir(dir);
}

static void print_process_page(const char *pid) {
   ProcessInfo process;

   if (get_process_status(pid, &process) != 0) {
      printf("<h1>Processo não encontrado</h1>\n");
      printf("<p>O processo com PID %s não está mais disponível.</p>\n", pid);
      printf("<p><a href='monitor'>Voltar para a página geral</a></p>\n");
      return;
   }

   ProcessCpuInfo cpu;
   int has_cpu_info = get_process_cpu_info(pid, &cpu) == 0;

   char command[PROCESS_COMMAND_SIZE];
   int has_command = get_process_command_line(pid, command, sizeof(command)) == 0;

   printf("<h2>Detalhes do processo</h2>\n");
   printf("<p><b>PID:</b> %u</p>\n", process.pid);
   printf("<p><b>PPID:</b> %u</p>\n", process.ppid);
   printf("<p><b>Nome:</b> %s</p>\n", process.name);
   printf("<p><b>Estado:</b> %s</p>\n", process.state);
   printf("<p><b>UID:</b> %u</p>\n", process.uid);
   printf("<p><b>Linha de comando:</b> ");

   if (has_command && command[0] != '\0')
      printf("%s", command);
   else
      printf("(vazia)");

   printf("</p>\n");
   printf("<p><b>Threads:</b> %u</p>\n", process.threads);
   printf("<p><b>Memória virtual:</b> %lu KB</p>\n", process.vm_size_kb);
   printf("<p><b>Memória residente:</b> %lu KB</p>\n", process.vm_rss_kb);

   if (has_cpu_info) {
      printf("<p><b>CPU modo usuário:</b> %.2f segundos</p>\n", clock_ticks_to_seconds(cpu.utime));
      printf("<p><b>CPU modo sistema:</b> %.2f segundos</p>\n", clock_ticks_to_seconds(cpu.stime));
      printf("<p><b>Prioridade:</b> %ld</p>\n", cpu.priority);
      printf("<p><b>Nice:</b> %ld</p>\n", cpu.nice);
   } else {
      printf("<p>Informações de CPU indisponíveis.</p>\n");
   }

   printf("<p><a href='monitor'>Voltar para a página geral</a></p>\n");
}

void print_general_page(void) {
   char kernel_version[128];
   ProcTime uptime;
   ProcTime idle;
   char rtc_date[64];
   char rtc_time[64];
   char cpu_model[256];
   double cpu_frequency;
   int cpu_cores;
   LoadAverage load;
   MemoryInfo memory;

   /* Kernel */
   printf("<h2>Sistema e Kernel</h2>\n");

   if (get_kernel_version(kernel_version, sizeof(kernel_version)) == 0)
      printf("<p>Kernel: %s</p>\n", kernel_version);

   /* Uptime */
   printf("<h2>Uptime</h2>\n");

   if (get_uptime(&uptime, &idle) == 0) {
      printf("<p>Uptime: %lu dias, %lu horas, %lu minutos e %lu segundos</p>\n",
         uptime.days, uptime.hours, uptime.minutes, uptime.seconds);

      printf("<p>Tempo ocioso: %lu dias, %lu horas, %lu minutos e %lu segundos</p>\n",
         idle.days, idle.hours, idle.minutes, idle.seconds);
   }

   /* RTC */
   printf("<h2>Data e hora</h2>\n");

   if (get_rtc(rtc_date, sizeof(rtc_date), rtc_time, sizeof(rtc_time)) == 0)
      printf("<p>Data: %s</p>\n<p>Hora: %s</p>\n", rtc_date, rtc_time);

   /* CPU */
   printf("<h2>Processador</h2>\n");

   if (get_cpu_info(cpu_model, sizeof(cpu_model), &cpu_frequency, &cpu_cores) == 0) {
      printf("<p>Modelo: %s</p>\n", cpu_model);
      printf("<p>Frequência: %.2f MHz</p>\n", cpu_frequency);
      printf("<p>Núcleos: %d</p>\n", cpu_cores);
   }

   /* Load */
   printf("<h2>Carga do sistema</h2>\n");

   if (get_load_average(&load) == 0)
      printf("<p>1 minuto: %.2f</p>\n<p>5 minutos: %.2f</p>\n<p>15 minutos: %.2f</p>\n",
         load.one, load.five, load.fifteen);

   /* CPU usage */
   double cpu_usage;

   printf("<h2>Ocupação do processador</h2>\n");

   int result = get_cpu_usage(&cpu_usage);

   if (result == 0)
      printf("<p>Ocupação: %.2f%%</p>\n", cpu_usage);
   else if (result == 1)
      printf("<p>Medição sendo calculada...</p>\n");
   else
      printf("<p>Erro ao obter ocupação do processador.</p>\n");

   /* Memory */
   printf("<h2>Memória RAM</h2>\n");

   if (get_memory(&memory) == 0)
      printf("<p>Total: %lu MB</p>\n<p>Usada: %lu MB</p>\n", memory.total_mb, memory.used_mb);

   /* Disk I/O */
   printf("<h2>Operações de entrada e saída</h2>\n");
   get_disk_stats();

   /* Filesystems */
   printf("<h2>Sistemas de arquivos</h2>\n");
   printf("<ul>\n");
   get_filesystems();
   printf("</ul>\n");

   /* Devices */
   printf("<h2>Dispositivos</h2>\n");
   get_devices();

   /* Network */
   printf("<h2>Dispositivos de rede</h2>\n");
   printf("<ul>\n");
   get_network_devices();
   printf("</ul>\n");
}

/* Main */

int main(void) {
   printf("Content-Type: text/html\r\n");
   printf("\r\n");

   printf("<!DOCTYPE html>\n");
   printf("<html>\n");


   printf("<head>\n");
   printf("    <meta charset='UTF-8'>\n");
   printf("    <meta http-equiv='refresh' content='5'>\n");
   printf("    <title>System Monitor</title>\n");
   printf("</head>\n");

   printf("<body>\n");

   char pid[32];

   if (get_requested_pid(pid, sizeof(pid))) {
      /* /cgi-bin/monitor?pid=N */
      print_process_page(pid);
   } else {
      /* /cgi-bin/monitor */
      printf("<h1>System Monitor</h1>\n");
      print_general_page();
      printf("<hr>\n");
      print_process_list();
   }

   printf("</body>\n");
   printf("</html>\n");

   return 0;
}
