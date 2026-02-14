# ```The code is vibe coded LMAO``` 

There is javascript in the project why?
- I come from a JavaScript background(beginner) and wanted to learn more about C and Raylib and I dont want to learn cmake for now I just want to quickly create this project and use it on my day to day tasks as a Virtual Assistant and for my personal ```"CODING"``` projects. (I know it's not the best option and a noob move, but it's a learning experience)


What is kOrT?
- I am quite lazy and wanted to automate repetitive tasks, in this case
  opening bunch of folders, files, browser for my projects (VA and personal ```"CODING"``` projects)
  and I realize you can acutally automate it using scripts.

- Kort is simply a script manager, you can create script (.bat) and click them to run them.
  - all the script are stores in `/scripts`
    - so you can manually create scripts and just copy paste them into the `/scripts` folder. Cause the kOrT script editor is shit




## How to run (you need C compiler installed in your computer) 
- [sourceforge - MinGW-w64](https://sourceforge.net/projects/mingw-w64/):
- [github - MinGW-w64](https://github.com/mingw-w64/mingw-w64):


- If you have nodejs installed, you can run `npm run:build:dev`

or
 ```
gcc ./src/main.c -o kort.exe -g -O0 -Wall -I./src/include -L./src/lib -lraylib -lgdi32 -lwinmm && ./kort

```

## Plans
- I want to understand the code first and figure out how to fix the command/script editor (without AI) hopefully I can fix it on my own.
- Add more feature maybe instead of manually creating bash scripts you can we can have a GUI for that make it like a ``` NO CODE ``` thingy
.....


## Acknowledgements
- [Raylib](https://github.com/raysan5/raylib)
- [MinGW-w64](https://github.com/mingw-w64/mingw-w64)
- [Node.js](https://nodejs.org/en/)


### NOTE: I only tried building it on Windows
