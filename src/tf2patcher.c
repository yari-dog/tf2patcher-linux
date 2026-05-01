/*!
 * tf2patcher.c
 * TF2 decal tool patcher
 * (c) default-username 2020
 */

#include "base.h"
#include "common.h"
#include "memory.h"

// platform-dependent code
// TODO implement these for linux
#ifdef WINDOWS
// the memory values to use for patching
// CConfirmCustomizeTextureDialog::PerformFilter
unsigned char pattern[] = {0xBA, 0x04, 0x00, 0x00, 0x00, 0x48, 0x8B,
                           0xFF, 0xFF, 0x90, 0xFF, 0xFF, 0x00, 0x00,
                           0x48, 0x8B, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                           0xE8, 0xFF, 0xFF, 0xFF, 0xFF, 0x85, 0xFF};
int addr_bump = 21;

unsigned char pattern2[] = {0x80, 0x3D, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x74};
unsigned char patch2[] = {0xEB};
int patch_sz2 = 1;

// acquire hwnd to tf2 window
// wait if the window is not there yet
HWND get_tf2_window(void) {
  const char *window_name = "Team Fortress 2 - Direct3D 9 - 64 Bit";

  HWND window = FindWindow(NULL, window_name);
  if (!window) {
    printf("Waiting for TF2 to start...\n");

    do {
      Sleep(1000);
      window = FindWindow(NULL, window_name);
    } while (!window);
  }

  verbose_print("Found TF2 window with HWND %u\n", window);
  return window;
}

// find client.dll in tf2 module table
// wait for max 15 seconds if it's not yet loaded
HMODULE get_tf2_client_module(HANDLE process) {
  HMODULE modules[8192];
  DWORD num_modules;
  char module_name[PATH_MAX], tempbuf[PATH_MAX], error_buf[8192];
  size_t num_retry = 0;

  do {
    if (!EnumProcessModulesEx(process, modules, sizeof(modules), &num_modules,
                              LIST_MODULES_ALL)) {
      fprintf(stderr, "Cannot enumerate TF2 modules: %s\n",
              describe_error(error_buf, sizeof(error_buf)));
      return 0;
    } else {
      assert(num_modules % sizeof(HMODULE) == 0);
      num_modules /= sizeof(HMODULE);

      verbose_print("Found %u modules in TF2 process\n", num_modules);

      for (size_t i = 0; i < num_modules; i++) {
        if (GetModuleFileNameEx(process, modules[i], module_name,
                                sizeof(module_name)) &&
            !strcasecmp(
                extract_file_name(module_name, tempbuf, sizeof(tempbuf)),
                "client.dll")) {
          verbose_print("Found TF2 client.dll (%zu/%u)\n", i + 1, num_modules);
          return modules[i];
        }
      }
    }

    Sleep(1000);
  } while (num_retry++ <= 30);

  fprintf(stderr, "Failed to find TF2 client.dll module!\n");
  return 0;
}

bool attach_to_tf2(void) {
  char error_buf[8192];

  HWND window = get_tf2_window();

  DWORD process_id;
  GetWindowThreadProcessId(window, &process_id);
  HANDLE process = OpenProcess(PROCESS_ALL_ACCESS, FALSE, process_id);

  if (!process) {
    fprintf(stderr, "Cannot attach to TF2 process: %s\n",
            describe_error(error_buf, sizeof(error_buf)));
  } else {
    verbose_print("Attached to TF2 process with PID %u\n", process_id);

    HMODULE module = get_tf2_client_module(process);
    if (module) {
      pinfo.process = process;
      pinfo.module = module;

      return true;
    }

    CloseHandle(process);
  }

  return false;
}

bool calc_client_module_bounds(void) {
  pinfo.cl_base = (unsigned char *)pinfo.module;

  IMAGE_DOS_HEADER dos_hdr;
  IMAGE_NT_HEADERS nt_hdr;

  if (read_mem(pinfo.cl_base, &dos_hdr, sizeof(dos_hdr)) &&
      read_mem(pinfo.cl_base + dos_hdr.e_lfanew, &nt_hdr, sizeof(nt_hdr))) {
    pinfo.cl_size = nt_hdr.OptionalHeader.SizeOfImage;
    verbose_print("TF2 client.dll module: 0x%" PRIXPTR " with sz=%zu\n",
                  (uintptr_t)pinfo.cl_base, pinfo.cl_size);
    return true;
  }

  fprintf(stderr, "Failed to calculate TF2 client.dll module size!\n");
  return false;
}

void free_resources(void) {
  if (pinfo.process) {
    CloseHandle(pinfo.process);
    pinfo.process = 0;
  }
}

#elif defined(LINUX)
// the memory values to use for patching
// CConfirmCustomizeTextureDialog::PerformFilter
unsigned char pattern[] = {0x48, 0x89, 0xFF, 0xFF, 0x48, 0x8b, 0xFF,
                           0x48, 0x8b, 0xFF, 0xFF, 0x90, 0xFF, 0xFF,
                           0x00, 0x00, 0x48, 0x8B, 0xFF, 0xFF, 0xFF,
                           0xFF, 0xFF, 0xE8, 0xFF, 0xFF, 0xFF, 0xFF};
int addr_bump = 23;

// NOTE: this patches a different jump to the windows version. it gives the
// same effect, i just found this JLE before i found the JZ.
unsigned char pattern2[] = {0x80, 0xA0, 0xFF, 0xFF, 0xFF, 0xFF, 0xC0, 0x0F};
unsigned char patch2[] = {0x48, 0xE9};
int patch_sz2 = 2;

bool get_client_so(void) { return 1; }

bool attach_to_tf2(void) {
  // partly from equip region patcher by rsed
  // https://raw.githubusercontent.com/rsedxcftvgyhbujnkiqwe/tf2-equip-region-patcher-linux/refs/heads/master/tf2-patcher.c
  // find pid by name
  pid_t pid = -1;
  char cmd[] = "pgrep tf_linux64";
  FILE *fp = popen(cmd, "r");
  if (!fp) {
    perror("popen");
    return 0;
  }

  if (fscanf(fp, "%d", &pid) != 1) {
    printf("fscanf !=1");
    return 0;
  }

  pclose(fp);

  printf("found pid: %d\n", pid);
  // attach
  if (ptrace(PTRACE_ATTACH, pid, NULL, NULL) == -1) {
    perror("ptrace attach");
    return 0;
  }
  waitpid(pid, NULL, 0);

  pinfo.pid = pid;

  // not gonna refactor the windows stuff so jus return the 1
  return 1;
};

void free_resources(void) {
  if (ptrace(PTRACE_DETACH, pinfo.pid, NULL, NULL) == -1)
    perror("ptrace detace");
  close(pinfo.mem_fd);
}

bool calc_client_module_bounds(void) {
  // scan /proc/PID/maps for client.so
  char maps_filename[256], mem_filename[256];
  snprintf(maps_filename, sizeof(maps_filename), "/proc/%d/maps", pinfo.pid);
  snprintf(mem_filename, sizeof(mem_filename), "/proc/%d/mem", pinfo.pid);

  FILE *maps_file = fopen(maps_filename, "r");
  int mem_fd = open(mem_filename, O_RDWR);

  if (!maps_file || mem_fd == -1) {
    perror("Error opening maps or mem file");
    if (maps_file)
      fclose(maps_file);
    if (mem_fd != -1)
      close(mem_fd);
    return 0;
  }

  // search maps for /client.so
  unsigned long g_start_addr = 0, g_end_addr = 0;
  char line[256];
  while (fgets(line, sizeof(line), maps_file))
    if (strstr(line, "/client.so")) {
      unsigned long start_addr = 0, end_addr = 0;
      if (sscanf(line, "%lx-%lx", &start_addr, &end_addr) == 2) {
        // either set entrypoint (if contiguous) or do something undefined if
        // not idk
        if (g_end_addr != start_addr) {
          printf("g_e_a (%lx) != s_a (%lx), setting g_s_a (%lx) to s_a (if you "
                 "see this more than once fix it)\n",
                 g_end_addr, start_addr, g_start_addr);
          g_start_addr = start_addr;
        }
        g_end_addr = end_addr;
      }
    }

  if (!g_start_addr && !g_end_addr)
    return 0;

  pinfo.mem_fd = mem_fd;
  pinfo.cl_base = (unsigned char *)g_start_addr;

  pinfo.cl_size = g_end_addr - g_start_addr;
  printf("start: %lx, end: %lx, size: %lx\n", g_start_addr, g_end_addr,
         pinfo.cl_size);

  fclose(maps_file);

  if (lseek(mem_fd, g_start_addr, SEEK_SET) < 0) {
    perror("lseek");
    return 0;
  }

  return 1;
};

#endif

// perform new patching method thanks to leaked tf2 source code
bool do_patch(void) {
  printf("Patching...\n");

  // CConfirmCustomizeTextureDialog::PerformFilter
  unsigned char *addr = find_mem_cl(pattern, sizeof(pattern));
  if (!addr) {
    fprintf(stderr,
            "Failed to find CConfirmCustomizeTextureDialog::PerformFilter "
            "pattern in client library!\n");
  } else {
    verbose_print("CConfirmCustomizeTextureDialog::PerformFilter pattern addr: "
                  "0x%" PRIXPTR " (offset: 0x%" PRIXPTR ")\n",
                  (uintptr_t)addr, (uintptr_t)(addr - pinfo.cl_base));

    // rewrite call to mov in order to force identity filter
    addr += addr_bump;
    set_mem(addr, (unsigned char[]){0xB8, 0x01, 0x00, 0x00, 0x00}, 5);
    verbose_print("Rewrote CALL to MOV\n");

    // disable blending
    // check memory first
    addr = find_mem(pattern2, sizeof(pattern2), addr, 200);
    if (!addr) {
      fprintf(stderr,
              "Failed to find CConfirmCustomizeTextureDialog::PerformFilter "
              "pattern #2 in client library!\n");
    } else {
      verbose_print("Found pattern 2 0x%" PRIXPTR "\n", (uintptr_t)addr);
      // rewrite jz to jmp
      addr += 7;
      set_mem(addr, patch2, patch_sz2);
      verbose_print("Rewrote JZ to JMP\n");

      // and thats pretty much it
      printf("Done!\n");
      return true;
    }
  }

  return false;
}

int main(int argc, char *argv[]) {
  printf("  -------------------------------------------\n"
         "  |       TF2 decal tool patcher 2.0.5       |\n"
         "  | (c) default-username, Apr 2020, Mar 2016 |\n"
         "  |                        Updated June 2024 |\n"
#ifdef LINUX
         "  | linux port by yari             Apr  2026 |\n"
#endif
         "  -------------------------------------------\n"
         "\n");

  if (argv) {
    for (int i = 0; i < argc; i++) {
      if (argv[i]) {
        if (!strcasecmp(argv[i], "-h") || !strcasecmp(argv[i], "--help")) {
          printf("Usage: tf2patcher -[hv]\n"
                 "       -h | --help    : show this help\n"
                 "       -v | --verbose : be more verbose\n");
          return EXIT_SUCCESS;
        } else if (!strcasecmp(argv[i], "-v") ||
                   !strcasecmp(argv[i], "--verbose")) {
          pinfo.verbose_mode = true;
        }
      }
    }
  }

  if (pinfo.verbose_mode) {
    printf("Running in verbose mode\n");
  }

  bool res = attach_to_tf2() && calc_client_module_bounds() && do_patch();
  //
  free_resources();

  printf("Press ENTER to exit...\n");
  getchar();

  return res ? EXIT_SUCCESS : EXIT_FAILURE;
}
