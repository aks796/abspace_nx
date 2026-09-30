/* abs_extras.c -- the port's own pages for the tiny planets' links: the
 * fallback, used only when abs_ctl.lua could not make the game's own popup
 * (or a URL reached Launcher.openURL), and the list of the videos on the SD
 * card that the game's popups offer.
 *
 * On the episode screen, small planets beside the big ones (the space
 * station, a Mars rover, Apollo, Europa, Facebook, Twitter...) and a few
 * buttons elsewhere called url.prompt(id): "leave the game?", then a web page
 * -- NASA's articles and videos, mostly. There is no browser here, and the
 * pages are long gone, so abs_ctl.lua hands the link's name (NASA_URL...) to
 * this file instead, which shows a page about its topic over the game, and
 * plays its video if the player put one on the SD card:
 *     sd:/switch/abspace_nx/videos/<topic>.mp4     (topic: the list below, e.g.
 *                                                nasa.mp4 for the ISS video)
 * The texts are the port's own. A link that arrives as a URL (the game's
 * Launcher.openURL) is matched by its t= parameter.
 *
 * Controls: left/right choose, A presses, B closes; touch works too. MIT.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>
#include <switch.h>

#include "abs.h"
#include "config.h"
#include "dcr_config.h"
#include "util.h"

const char *dcr_game_root(void); /* main.c */

typedef struct {
  const char *id;    /* url.lua's name */
  const char *topic; /* the URL's t= (and the video's file name) */
  const char *title;
  const char *text;
} Topic;

#define OFFLINE "The Switch edition plays offline, so there is no page to open here."

static const Topic k_topics[] = {
    {"NASA_URL", "nasa", "The International Space Station",
     "In 2012 NASA astronaut Don Pettit, aboard the International Space Station, used a plush bird and a "
     "slingshot to show how things fly in orbit. Everything on the station is falling around the Earth "
     "together, so a launched bird does not curve down as it would at home: it keeps going straight until "
     "it hits something.\n"
     "The station goes round the Earth about every 90 minutes, some 400 km up, and has had people living on "
     "it since November 2000."},
    {"ISS_URL", "iss", "The International Space Station",
     "The largest thing people have built in space: as long as a football field, assembled in orbit from "
     "1998 on by the United States, Russia, Europe, Japan and Canada. It circles the Earth about 16 times a "
     "day, some 400 km up, and has had a crew aboard since November 2000."},
    {"CURIOSITY_URL", "curiosity", "Curiosity on Mars",
     "NASA's Curiosity rover landed in Gale Crater on 6 August 2012, lowered to the ground on cables by a "
     "rocket-powered \"sky crane\". The car-sized rover, powered by the heat of its plutonium, has been "
     "climbing Mount Sharp ever since, reading the rocks for signs that Mars could once have supported "
     "life."},
    {"PHOENIX_URL", "phoenix", "Phoenix",
     "The Phoenix lander touched down near the north pole of Mars in May 2008. Digging with its robotic arm, "
     "it found water ice just under the soil, before the long polar winter ended its mission."},
    {"VIKING_URL", "viking", "Viking",
     "Viking 1 landed on Mars on 20 July 1976, the first American spacecraft to work on its surface, joined "
     "by Viking 2 that September. The two landers sent back the first colour pictures from the ground and "
     "tested the soil for signs of life."},
    {"PATHFINDER_URL", "pathfinder", "Pathfinder and Sojourner",
     "Mars Pathfinder bounced down on airbags on 4 July 1997 and let out Sojourner, a rover the size of a "
     "microwave oven: the first wheels to roll on another planet."},
    {"SPIRIT_URL", "spirit", "Spirit",
     "Spirit landed in Gusev Crater in January 2004 for a mission of 90 days, and kept exploring for six "
     "years. Stuck in soft sand from 2009, it still found signs that hot water once flowed there."},
    {"OPPORTUNITY_URL", "opportunity", "Opportunity",
     "Opportunity landed three weeks after its twin Spirit in January 2004, also planned for 90 days. It "
     "drove on for over 14 years and more than 45 km -- longer than a marathon -- until a planet-wide dust "
     "storm in 2018 cut off its power."},
    {"BUZZ_APOLLO_URL", "buzz", "Buzz Aldrin and Apollo 11",
     "On 20 July 1969 Neil Armstrong and Buzz Aldrin landed on the Moon in the lunar module Eagle, while "
     "Michael Collins kept Columbia in orbit above them. Aldrin, the second person to walk on the Moon, has "
     "spent the decades since arguing for people to go on to Mars."},
    {"EUROPA_URL", "europamoon", "Europa",
     "Europa, one of Jupiter's four big moons, is a little smaller than ours. Under its cracked shell of ice "
     "there is thought to be a deep salty ocean with more water than all of Earth's -- one of the best "
     "places to look for life beyond our planet. NASA's Europa Clipper, launched in October 2024, is on its "
     "way to take a closer look."},
    {"NEW_HORIZON_URL", "newhorizons", "New Horizons",
     "Launched in January 2006, New Horizons flew past Pluto on 14 July 2015, nine and a half years and "
     "almost 5 billion km later, and sent back the first close-up pictures of it -- among them the great "
     "heart-shaped plain of ice."},
    {"DEEPIMPACT_URL", "deepimpact", "Deep Impact",
     "In July 2005 the Deep Impact spacecraft fired a copper impactor into Comet Tempel 1 and watched from a "
     "safe distance what the crash threw up from under the comet's crust."},
    {"DAWN_URL", "dawn", "Dawn",
     "Driven by ion engines, Dawn orbited the giant asteroid Vesta and then the dwarf planet Ceres: the "
     "first spacecraft to orbit two worlds beyond the Earth and the Moon."},
    {"OSIRISREX_URL", "osirisrex", "OSIRIS-REx",
     "OSIRIS-REx touched the asteroid Bennu in October 2020 and grabbed a handful of its rock and dust, "
     "which it dropped off at Earth in a capsule in September 2023."},
    {"ORION_URL", "orion", "Orion",
     "Orion is NASA's capsule for taking astronauts back to the Moon in the Artemis missions. Without a "
     "crew, it flew around the Moon and back on Artemis I at the end of 2022."},
    {"MERCURY_URL", "mercury", "Mercury",
     "The smallest planet and the closest to the Sun: a year there lasts 88 Earth days. With almost no air "
     "to hold heat, its days are hot enough to melt lead and its nights far colder than any on Earth."},
    {"VENUS_URL", "venus", "Venus",
     "Nearly Earth's twin in size, but its thick air of carbon dioxide traps so much heat that the ground is "
     "around 465 degrees -- the hottest planet, though Mercury is closer to the Sun. It also turns backwards "
     "compared with most planets."},
    {"EARTH_URL", "earth", "Earth",
     "The only world known to have life. About 71 percent of it is covered by water, and its magnetic field "
     "and atmosphere shield it from most of the Sun's harsh radiation."},
    {"MOON_URL", "moon", "The Moon",
     "About 384,000 km away, the Moon always shows us the same face. Twelve people walked on it between 1969 "
     "and 1972, and their footprints are still there: there is no wind to blow them away."},
    {"MARS_URL", "mars", "Mars",
     "Rust in its dust makes Mars red. It has the tallest volcano in the solar system, Olympus Mons, about "
     "two and a half times the height of Everest, and more robots working on it than any planet but ours."},
    {"ASTEROID_URL", "asteroid_belt", "The asteroid belt",
     "Between Mars and Jupiter circle millions of rocky leftovers from the birth of the planets. They are "
     "so spread out that spacecraft fly through without trouble. The biggest, Ceres, is a dwarf planet."},
    {"JUPITER_URL", "jupiter", "Jupiter",
     "The biggest planet: all the others would fit inside it. Its Great Red Spot is a storm larger than "
     "Earth that has been raging for centuries, and it has dozens of moons."},
    {"SOLARSYSTEM_EUROPA_URL", "europa", "Europa",
     "Jupiter's icy moon Europa probably hides a salty ocean under its crust, with more water than all of "
     "Earth's oceans together."},
    {"SATURN_URL", "saturn", "Saturn",
     "Its rings are made of countless pieces of ice and rock, from grains to boulders the size of houses. "
     "Saturn is so light for its size that it would float in water, if a bath big enough existed."},
    {"URANUS_URL", "uranus", "Uranus",
     "An ice giant tipped on its side: it rolls around the Sun, so each pole gets about 42 years of "
     "daylight and then 42 years of night."},
    {"NEPTUNE_URL", "neptune", "Neptune",
     "The farthest planet from the Sun, found in 1846 by mathematics before anyone saw it. It has the "
     "fastest winds in the solar system, over 2,000 km an hour."},
    {"PLUTO_URL", "pluto", "Pluto",
     "Called a planet from its discovery in 1930 until 2006, when it became a dwarf planet. New Horizons "
     "showed it has mountains of water ice and a huge heart-shaped glacier."},
    {"COMETS_URL", "comet", "Comets",
     "Dirty snowballs of ice and dust from the edge of the solar system. Near the Sun they warm up and grow "
     "glowing tails millions of km long. Halley's comet comes back every 76 years or so -- next in 2061."},
    {"MOVIE_URL", "spacetrailer", "Angry Birds Space",
     "The trailer of Angry Birds Space (2012): the pigs have stolen the eggs again, this time into space, "
     "where planets pull the birds into orbit and everything floats free outside their gravity."},
    {"FACEBOOK_URL", "facebook", "Angry Birds on Facebook", "This tiny planet opened Angry Birds' Facebook page. " OFFLINE},
    {"TWITTER_URL", "twitterfollow", "Angry Birds on Twitter", "This tiny planet followed Angry Birds on Twitter. " OFFLINE},
    {"WEIBO_URL", "weibo", "Angry Birds on Weibo", "This link opened Angry Birds' Weibo page. " OFFLINE},
    {"QZONE_URL", "qzone", "Angry Birds on Qzone", "This link opened Angry Birds' Qzone page. " OFFLINE},
    {"TENCENT_URL", "tencentweibo", "Angry Birds on Tencent Weibo", "This link opened Angry Birds' Tencent Weibo page. " OFFLINE},
    {"RENREN_URL", "renren", "Angry Birds on Renren", "This link opened Angry Birds' Renren page. " OFFLINE},
    {"AB_FRIENDS_URL", "angrybirdsfriendsfull", "Angry Birds Friends",
     "This tiny planet sent players to the app store for Angry Birds Friends, the tournament game. " OFFLINE},
    {"STARWARS2_URL", "angrybirdsstarwars2full", "Angry Birds Star Wars II",
     "This tiny planet advertised Angry Birds Star Wars II (2013) in the app stores. " OFFLINE},
    {"STARWARS2_URL_PREMIUM", "angrybirdsstarwars2paid", "Angry Birds Star Wars II",
     "This tiny planet advertised Angry Birds Star Wars II (2013) in the app stores. " OFFLINE},
    {"FULL_GAME_URL", "angrybirdsspacefull", "Angry Birds Space", "This link opened the game's store page. " OFFLINE},
    {"KELLOGGSPROMO_URL", "kelloggspromo", "Kellogg's", "This link opened a Kellogg's promotion page. " OFFLINE},
    {"OCEANELDERS_URL", "oceanelders", "Ocean Elders", "This link opened the page of Ocean Elders, a group working to protect the oceans. " OFFLINE},
    {"SAMSUNG_URL", "galaxyvideo", "Samsung Galaxy", "This link opened a promotional video. " OFFLINE},
    {"TOONS_URL", "toons", "Angry Birds Toons", "This link opened Rovio's YouTube channel. " OFFLINE},
    {"PRIVACY_POLICY_URL", "privacypolicy", "Privacy policy", "Rovio's privacy policy was on the web. " OFFLINE},
    {"EULA_URL", "eula", "Terms of use", "Rovio's terms of use were on the web. " OFFLINE},
};
#define NTOPICS ((int)(sizeof k_topics / sizeof k_topics[0]))

static Mutex g_lock;
static struct {
  int active;
  const Topic *t;
  char title[96];
  char text[1200];
  char video[300];  /* the video's path, "" if not on the card */
  char vname[64];   /* its file name, for the hint */
  int sel;          /* 0 video, 1 back */
  float bx[2], by, bw[2], bh; /* the buttons, for touch */
  int bvalid;
} E;

void abs_extras_init(void) {}

/* the .mp4 files in videos/ on the card, read once: the names the game's popups offer */
const char *abs_extras_videos(void) {
  static char list[480];
  static int done;
  if (done)
    return list;
  done = 1;
  char dir[300];
  snprintf(dir, sizeof dir, "%s/videos", dcr_game_root());
  DIR *d = opendir(dir);
  if (!d)
    return list;
  struct dirent *e;
  size_t len = 0;
  int n = 0;
  while ((e = readdir(d))) {
    const char *name = e->d_name;
    size_t l = strlen(name);
    if (l < 5 || l > 60 || strcasecmp(name + l - 4, ".mp4"))
      continue;
    int ok = 1;
    for (size_t i = 0; i + 4 < l; i++)
      if (!((name[i] >= 'a' && name[i] <= 'z') || (name[i] >= 'A' && name[i] <= 'Z') ||
            (name[i] >= '0' && name[i] <= '9') || name[i] == '_'))
        ok = 0;
    if (!ok || len + l - 4 + 2 >= sizeof list)
      continue;
    for (size_t i = 0; i + 4 < l; i++)
      list[len++] = (char)((name[i] >= 'A' && name[i] <= 'Z') ? name[i] + 32 : name[i]);
    list[len++] = ' ';
    list[len] = 0;
    n++;
  }
  closedir(d);
  if (len)
    list[--len] = 0;
  debugPrintf("[extras] %d video%s in %s: %s\n", n, n == 1 ? "" : "s", dir, list);
  return list;
}

static int exists(const char *path) {
  struct stat st;
  return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

void abs_extras_open(const char *link) {
  if (!link || !*link)
    return;
  char key[64] = "";
  const char *t = strstr(link, "t=");
  if (strstr(link, "://") && t) {
    /* a URL: its t= parameter */
    t += 2;
    size_t n = strcspn(t, "&#");
    snprintf(key, sizeof key, "%.*s", (int)(n < sizeof key ? n : sizeof key - 1), t);
  } else {
    snprintf(key, sizeof key, "%s", link);
  }
  const Topic *tp = NULL;
  for (int i = 0; i < NTOPICS && !tp; i++)
    if (!strcasecmp(k_topics[i].id, key) || !strcasecmp(k_topics[i].topic, key))
      tp = &k_topics[i];
  abs_font_init();
  mutexLock(&g_lock);
  E.t = tp;
  if (tp) {
    snprintf(E.title, sizeof E.title, "%s", tp->title);
    snprintf(E.text, sizeof E.text, "%s", tp->text);
    snprintf(E.vname, sizeof E.vname, "%s.mp4", tp->topic);
  } else {
    snprintf(E.title, sizeof E.title, "A web link");
    snprintf(E.text, sizeof E.text, "This opened a web page (%s). " OFFLINE, key);
    snprintf(E.vname, sizeof E.vname, "%s.mp4", key);
  }
  char path[300];
  snprintf(path, sizeof path, "%s/videos/%s", dcr_game_root(), E.vname);
  E.video[0] = 0;
  if (abs_video_available() && exists(path))
    snprintf(E.video, sizeof E.video, "%s", path);
  E.sel = E.video[0] ? 0 : 1;
  E.bvalid = 0;
  E.active = 1;
  mutexUnlock(&g_lock);
  debugPrintf("[extras] %s: \"%s\"%s%s\n", key, E.title, E.video[0] ? ", video " : "", E.video);
}

int abs_extras_active(void) { return E.active; }

static void close_page(void) {
  mutexLock(&g_lock);
  E.active = 0;
  mutexUnlock(&g_lock);
}

static void press(int which) {
  if (which == 0 && E.video[0]) {
    char path[300];
    snprintf(path, sizeof path, "%s", E.video);
    if (!abs_video_play(path))
      debugPrintf("[extras] %s would not play\n", path);
    return; /* the page is still here when the video ends */
  }
  close_page();
}

void abs_extras_input(uint64_t down, uint64_t held, float lsx, float lsy, int touch, float tx, float ty) {
  (void)held, (void)lsy;
  if (!E.active)
    return;
  const uint64_t k_a = dcr_config()->swap_ab ? HidNpadButton_B : HidNpadButton_A;
  const uint64_t k_b = dcr_config()->swap_ab ? HidNpadButton_A : HidNpadButton_B;
  static int stick_was;
  int stick = lsx < -0.6f ? -1 : lsx > 0.6f ? 1 : 0;
  int move = 0;
  if (down & HidNpadButton_Left) move = -1;
  if (down & HidNpadButton_Right) move = 1;
  if (stick && stick != stick_was) move = stick;
  stick_was = stick;
  if (move && E.video[0])
    E.sel = E.sel ? 0 : 1;
  if (touch && E.bvalid) {
    for (int i = 0; i < 2; i++)
      if (E.bw[i] > 0 && tx >= E.bx[i] && tx < E.bx[i] + E.bw[i] && ty >= E.by && ty < E.by + E.bh) {
        press(i);
        return;
      }
  }
  if (down & k_a)
    press(E.sel);
  else if (down & (k_b | HidNpadButton_Plus))
    close_page();
}

/* the text cut into lines no wider than w (\n starts a paragraph) */
static int wrap(const char *msg, float px, float w, char lines[][160], int max) {
  int n = 0;
  char cur[160] = "";
  const char *p = msg;
  while (*p && n < max) {
    if (*p == '\n') {
      snprintf(lines[n++], 160, "%s", cur);
      if (n < max)
        lines[n++][0] = 0; /* a blank line between paragraphs */
      cur[0] = 0;
      p++;
      continue;
    }
    const char *e = p;
    while (*e && *e != ' ' && *e != '\n')
      e++;
    char word[160];
    snprintf(word, sizeof word, "%.*s", (int)(e - p), p);
    char trial[320];
    snprintf(trial, sizeof trial, "%s%s%s", cur, cur[0] ? " " : "", word);
    if (cur[0] && abs_ov_text_width(px, trial) > w) {
      snprintf(lines[n++], 160, "%s", cur);
      snprintf(cur, sizeof cur, "%s", word);
    } else {
      snprintf(cur, sizeof cur, "%.159s", trial);
    }
    p = *e == ' ' ? e + 1 : e;
  }
  if (cur[0] && n < max)
    snprintf(lines[n++], 160, "%s", cur);
  return n;
}

void abs_extras_draw(void) {
  mutexLock(&g_lock);
  if (!E.active) {
    mutexUnlock(&g_lock);
    return;
  }
  char title[96], text[1200], vname[64];
  memcpy(title, E.title, sizeof title);
  memcpy(text, E.text, sizeof text);
  memcpy(vname, E.vname, sizeof vname);
  const int has_video = E.video[0] != 0, sel = E.sel;
  const int is_link = E.t && strstr(E.t->text, OFFLINE) != NULL;
  mutexUnlock(&g_lock);

  const float W = (float)abs_surface_w(), H = (float)abs_surface_h(), k = H / 720.0f;
  abs_ov_rect(0, 0, W, H, 0x000814b8u);
  const float pw = W * 0.66f, px = (W - pw) * 0.5f;
  const float tpx = 34 * k, mpx = 23 * k, bpx = 25 * k, gap = 26 * k;
  static char lines[24][160];
  int nl = wrap(text, mpx, pw - 2 * gap, lines, 24);
  float hint = (!has_video && !is_link && abs_video_available()) ? mpx * 1.6f : 0;
  float ph = gap + tpx * 1.6f + nl * mpx * 1.32f + hint + gap + bpx * 2.0f + gap;
  if (ph > H * 0.92f)
    ph = H * 0.92f;
  const float py = (H - ph) * 0.5f, r = 22 * k;
  /* the panel: deep space blue, a lighter rim, a band behind the title */
  abs_ov_rrect(px - 3 * k, py - 3 * k, pw + 6 * k, ph + 6 * k, r + 3 * k, 0x7fb2ffd0u);
  abs_ov_rrect(px, py, pw, ph, r, 0x0b1633f8u);
  abs_ov_rrect(px + 8 * k, py + 8 * k, pw - 16 * k, tpx * 1.6f + gap - 8 * k, r - 8 * k, 0x1b2d5ef0u);
  /* a few stars */
  static const float stars[][2] = {{0.06f, 0.82f}, {0.93f, 0.18f}, {0.88f, 0.9f}, {0.12f, 0.3f},
                                   {0.5f, 0.95f},  {0.72f, 0.4f},  {0.3f, 0.62f}};
  for (unsigned i = 0; i < sizeof stars / sizeof stars[0]; i++)
    abs_ov_rrect(px + stars[i][0] * pw, py + stars[i][1] * ph, 3 * k, 3 * k, 1.5f * k, 0xffffff70u);
  float y = py + gap;
  abs_ov_text(px + gap, y - 4 * k, tpx, 0xffd84affu, title);
  y += tpx * 1.6f;
  for (int i = 0; i < nl && y + mpx < py + ph - bpx * 2.6f - hint; i++, y += mpx * 1.32f)
    abs_ov_text(px + gap, y, mpx, 0xe4ecffffu, lines[i]);
  if (hint > 0) {
    char h[160];
    snprintf(h, sizeof h, "Video: put %s in " DCR_ROOT_PATH "/videos/ to watch it here.", vname);
    abs_ov_text(px + gap, py + ph - gap - bpx * 2.0f - hint, mpx * 0.85f, 0x9fb4e0ffu, h);
  }
  /* buttons, right-aligned: [Watch the video] [Back] */
  const char *label[2] = {"Watch the video", "Back"};
  const float bh = bpx * 1.8f, by = py + ph - gap - bh;
  float bx = px + pw - gap;
  float bxs[2] = {0, 0}, bws[2] = {0, 0};
  for (int i = 1; i >= 0; i--) {
    if (i == 0 && !has_video)
      continue;
    float tw = abs_ov_text_width(bpx, label[i]) + 2 * gap;
    bx -= tw;
    bxs[i] = bx, bws[i] = tw;
    int on = i == sel;
    abs_ov_rrect(bx, by, tw, bh, bh * 0.5f, on ? 0xf2b705ffu : 0x2c4580ffu);
    if (on)
      abs_ov_ring(bx, by, tw, bh, bh * 0.5f, 3 * k, 0xffffffc0u);
    abs_ov_text(bx + gap, by + (bh - bpx) * 0.5f, bpx, on ? 0x14203effu : 0xffffffffu, label[i]);
    bx -= gap * 0.6f;
  }
  mutexLock(&g_lock);
  for (int i = 0; i < 2; i++)
    E.bx[i] = bxs[i], E.bw[i] = bws[i];
  E.by = by, E.bh = bh;
  E.bvalid = 1;
  mutexUnlock(&g_lock);
}
