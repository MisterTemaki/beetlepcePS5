// Beetle PCE PS5: the core alone, as fast as it goes (make bench): frames per second and the cost of a state.
//   build/bench/pce-bench <card.pce> [frames]
// SPDX-License-Identifier: MIT
#include "libretro.h"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
static std::vector<unsigned char> rom;
static retro_game_info_ext ext = {};
static bool env(unsigned cmd, void* data) {
  switch (cmd) {
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY: *(const char**)data = "."; return true;
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: return true;
    case RETRO_ENVIRONMENT_GET_GAME_INFO_EXT: *(const retro_game_info_ext**)data = &ext; return true;
    default: return false;
  }
}
static void video(const void*, unsigned, unsigned, size_t) {}
static void sample(int16_t, int16_t) {}
static size_t batch(const int16_t*, size_t n) { return n; }
static void poll() {}
static int16_t state(unsigned, unsigned, unsigned, unsigned) { return 0; }
int main(int argc, char** argv) {
  FILE* f = fopen(argv[1], "rb"); unsigned char b[65536]; size_t n; while ((n = fread(b,1,sizeof b,f))>0) rom.insert(rom.end(), b, b+n); fclose(f);
  retro_set_environment(env); retro_set_video_refresh(video); retro_set_audio_sample(sample); retro_set_audio_sample_batch(batch);
  retro_set_input_poll(poll); retro_set_input_state(state); retro_init();
  ext.data = rom.data(); ext.size = rom.size(); ext.ext = "pce";
  retro_game_info gi = {}; gi.data = rom.data(); gi.size = rom.size();
  if (!retro_load_game(&gi)) { puts("load failed"); return 1; }
  for (unsigned p = 0; p < 5; p++) retro_set_controller_port_device(p, RETRO_DEVICE_JOYPAD);
  int frames = argc > 2 ? atoi(argv[2]) : 3000;
  auto t0 = std::chrono::steady_clock::now();
  for (int i = 0; i < frames; i++) retro_run();
  double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  printf("%d frames in %.2f s: %.0f fps (%.2f ms/frame)\n", frames, s, frames / s, s * 1000 / frames);
  size_t ss = retro_serialize_size(); std::vector<unsigned char> st(ss);
  t0 = std::chrono::steady_clock::now();
  for (int i = 0; i < 1000; i++) retro_serialize(st.data(), ss);
  s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  printf("state %zu bytes, serialize %.3f ms\n", ss, s);
  return 0;
}
