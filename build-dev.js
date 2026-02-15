const { execSync } = require("child_process");
const os = require("os");
console.log("Running debug build (with console visible)...");
let gccCommand = "";
if (os.platform() === "win32") {
  // Step 1: Compile the icon resource
  console.log("Compiling Windows icon resource...");
  execSync("windres ./icon.rc ./icon.res --output-format=coff", { stdio: "inherit" });
  // Step 2: Build the .exe WITHOUT -mwindows (console will be visible for debugging)
  gccCommand =
    "gcc ./src/main.c ./icon.res -g -O0 -Wall -I./src/include -L./src/lib -lraylib -lgdi32 -lwinmm -lshell32 -o kort.exe";
} else if (os.platform() === "linux") {
  gccCommand =
    "gcc ./src/main.c -g -O0 -Wall -I./src/include -L./src/lib -lraylib -lGL -lm -ldl -lpthread -o kort";
} else if (os.platform() === "darwin") {
  gccCommand =
    "gcc ./src/main.c -g -O0 -Wall -I./src/include -L./src/lib -lraylib -framework OpenGL -framework Cocoa -framework IOKit -framework CoreAudio -o kort";
}
console.log(gccCommand);
execSync(gccCommand, { stdio: "inherit" });
console.log("Debug build finished successfully!");
