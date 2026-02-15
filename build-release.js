const { execSync } = require("child_process");
const os = require("os");
let gccCommand = "gcc ./src/main.c -o kort";
if (os.platform() === "win32") {
  // Step 1: Compile the icon resource
  console.log("Compiling Windows icon resource...");
  execSync("windres ./icon.rc ./icon.res --output-format=coff", { stdio: "inherit" });
  // Step 2: Update gcc command to include the resource
  gccCommand =
    "gcc ./src/main.c ./icon.res -O3 -Wall -s -I./src/include -L./src/lib -lraylib -lgdi32 -lwinmm -lshell32 -mwindows -o kort.exe";
} else if (os.platform() === "linux") {
  gccCommand += " -O3 -Wall -s -I./src/include -L./src/lib -lraylib -lGL -lm -ldl -lpthread";
} else if (os.platform() === "darwin") {
  gccCommand +=
    " -O3 -Wall -s -I./src/include -L./src/lib -lraylib -framework OpenGL -framework Cocoa -framework IOKit -framework CoreAudio";
}
console.log("Running build command:");
console.log(gccCommand);
execSync(gccCommand, { stdio: "inherit" });
console.log("Build finished successfully!");
