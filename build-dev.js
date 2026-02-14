const { execSync } = require("child_process");
const os = require("os");

let gccCommand = "gcc ./src/main.c -o kort";

if (os.platform() === "win32") {
  gccCommand += ".exe -g -O0 -Wall -I./src/include -L./src/lib -lraylib -lgdi32 -lwinmm";
} else if (os.platform() === "linux") {
  gccCommand += " -g -O0 -Wall -I./src/include -L./src/lib -lraylib -lGL -lm -ldl -lpthread";
} else if (os.platform() === "darwin") {
  gccCommand += " -g -O0 -Wall -I./src/include -L./src/lib -lraylib -framework OpenGL -framework Cocoa -framework IOKit -framework CoreAudio";
}

console.log("Running development build:");
console.log(gccCommand);
execSync(gccCommand, { stdio: "inherit" });
