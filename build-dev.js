const { execSync } = require("child_process");
const os = require("os");

console.log("Running development build...");

let gccCommand = "";

if (os.platform() === "win32") {
  // Step 1: Compile the icon resource
  console.log("Compiling Windows icon resource...");
  execSync("windres ./icon.rc ./icon.res --output-format=coff", { stdio: "inherit" });

  // Step 2: Build the .exe including the resource
  gccCommand =
    "gcc ./src/main.c ./icon.res -g -O0 -Wall -I./src/include -L./src/lib -lraylib -lgdi32 -lwinmm -o kort.exe";
} else if (os.platform() === "linux") {
  gccCommand =
    "gcc ./src/main.c -g -O0 -Wall -I./src/include -L./src/lib -lraylib -lGL -lm -ldl -lpthread -o kort";
} else if (os.platform() === "darwin") {
  gccCommand =
    "gcc ./src/main.c -g -O0 -Wall -I./src/include -L./src/lib -lraylib -framework OpenGL -framework Cocoa -framework IOKit -framework CoreAudio -o kort";
}

console.log(gccCommand);
execSync(gccCommand, { stdio: "inherit" });

console.log("Build finished successfully!");
