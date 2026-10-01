/* abs_setup_plan.c -- Angry Birds Space's part of the first launch: its plan
 * for the runtime's dcr_setup.c, which does the work.
 *
 * The bar, in permille of the whole first launch (as the port's own had it):
 *   100- 500  libAngryBirdsSpace.so unpacked (by bytes)
 *   550- 950  the Java class list (by dex file)
 *        1000 the game starts
 *
 * .setup keys, which must not change (or every player unpacks again once):
 * libAngryBirdsSpace.so, classes.txt. MIT.
 */
#include "dcr_setup.h"

static const char *const k_libs[] = {"libAngryBirdsSpace.so"};

const RtSetupPlan port_setup_plan = {
    .libs = k_libs,
    .nlibs = 1,
    .libs_what = "Unpacking the game",
    .apk_requirement = "This port needs Angry Birds Space HD 2.2.14 (com.rovio.angrybirdsspaceHD,\n"
                       "armeabi-v7a): use the APK of that version.",
    .libs_p0 = 100,
    .libs_p1 = 500,
    .classes_p0 = 550,
    .classes_p1 = 950,
};
