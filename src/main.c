#include "raylib.h"
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <stdlib.h>

// Platform detection
#ifdef _WIN32
    #define PLATFORM_WINDOWS
    #define WIN32_LEAN_AND_MEAN
    #define NOGDI
    #define NOUSER
    #include <windows.h>
    #undef DrawText
    #undef CloseWindow
    #undef ShowCursor
#else
    #define PLATFORM_LINUX
    #include <unistd.h>
    #include <sys/stat.h>
    #include <sys/types.h>
#endif

#define MAX_INPUT_CHARS 100
#define screenWidth 1000
#define screenHeight 650
#define intialFPS 60
#define MAX_FILES 256
#define fontSize 18
#define MAX_SCRIPT_SIZE 65536
#define MAX_FILENAME_CHARS 50
#define MAX_COMMAND_CHARS 2000
#define MAX_UNDO_STACK 50

typedef struct {
    char displayName[256];
    char filePath[512];
    char fileExtension[16];
    Rectangle bounds;
    Rectangle editBounds;
    Rectangle deleteBounds;
    bool isExecuting;
} FileItem;

typedef struct {
    bool isOpen;
    char filename[MAX_FILENAME_CHARS + 1];
    char command[MAX_COMMAND_CHARS + 1];
    int filenameLength;
    int commandLength;
    bool filenameActive;
    bool commandActive;
    int framesCounter;
    bool isEditMode;
    int editIndex;
    Rectangle filenameBox;
    Rectangle commandBox;
    float commandScrollOffsetY;
    float commandScrollOffsetX;
    float commandMaxScrollY;
    float commandMaxScrollX;
    // Undo/Redo stacks
    char undoStack[MAX_UNDO_STACK][MAX_COMMAND_CHARS + 1];
    int undoStackSize;
    char redoStack[MAX_UNDO_STACK][MAX_COMMAND_CHARS + 1];
    int redoStackSize;
} Modal;

typedef struct {
    Rectangle container;
    float scrollOffset;
    float maxScroll;
} ScrollableList;

// Get absolute path to scripts directory
void getScriptsPath(char *buffer, size_t bufferSize) {
#ifdef PLATFORM_WINDOWS
    char exePath[512];
    GetModuleFileNameA(NULL, exePath, sizeof(exePath));

    // Find last backslash
    char *lastSlash = strrchr(exePath, '\\');
    if (lastSlash != NULL) {
        *lastSlash = '\0';
    }

    snprintf(buffer, bufferSize, "%s\\scripts", exePath);

    // Create directory if it doesn't exist
    CreateDirectoryA(buffer, NULL);
#else
    char exePath[512];
    ssize_t len = readlink("/proc/self/exe", exePath, sizeof(exePath) - 1);
    if (len != -1) {
        exePath[len] = '\0';
        char *lastSlash = strrchr(exePath, '/');
        if (lastSlash != NULL) {
            *lastSlash = '\0';
        }
        snprintf(buffer, bufferSize, "%s/scripts", exePath);
    } else {
        // Fallback to current directory
        char cwd[512];
        if (getcwd(cwd, sizeof(cwd)) != NULL) {
            snprintf(buffer, bufferSize, "%s/scripts", cwd);
        } else {
            snprintf(buffer, bufferSize, "./scripts");
        }
    }

    // Create directory if it doesn't exist
    mkdir(buffer, 0755);
#endif
}

// Read file content
char* readFileContent(const char *filepath) {
    FILE *file = fopen(filepath, "r");
    if (file == NULL) {
        printf("Failed to open file: %s\n", filepath);
        return NULL;
    }

    char *buffer = (char*)malloc(MAX_SCRIPT_SIZE);
    if (buffer == NULL) {
        fclose(file);
        return NULL;
    }

    size_t bytesRead = fread(buffer, 1, MAX_SCRIPT_SIZE - 1, file);
    buffer[bytesRead] = '\0';
    fclose(file);
    return buffer;
}

// Execute file content as command using system()
void executeFileContent(const char *filepath) {
    printf("Executing commands from: %s\n", filepath);

    char *content = readFileContent(filepath);
    if (content == NULL) {
        printf("Failed to read file content\n");
        return;
    }

    printf("Command content:\n%s\n", content);

#ifdef PLATFORM_WINDOWS
    char tempPath[2048] = {0};
    char tempFile[2048] = {0};

    if (GetTempPathA(sizeof(tempPath) - 20, tempPath) == 0) {
        strcpy(tempPath, "C:\\Temp\\");
    }

    // Ensure path ends with backslash
    size_t len = strlen(tempPath);
    if (len > 0 && tempPath[len-1] != '\\') {
        strcat(tempPath, "\\");
    }

    strcat(tempFile, tempPath);
    strcat(tempFile, "kort_exec.bat");

    FILE *temp = fopen(tempFile, "w");
    if (temp) {
        fprintf(temp, "@echo off\n");
        fprintf(temp, "%s\n", content);
        fprintf(temp, "echo.\n");
        fprintf(temp, "echo Press any key to close...\n");
        // fprintf(temp, "pause > nul\n"); // Disable pause close the terminal right away
        fclose(temp);

        char command[4096];
        snprintf(command, sizeof(command), "start cmd /c \"%s\"", tempFile);
        system(command);
    }
#else
    char tempFile[512];
    snprintf(tempFile, sizeof(tempFile), "/tmp/kort_exec.sh");

    FILE *temp = fopen(tempFile, "w");
    if (temp) {
        fprintf(temp, "#!/bin/bash\n");
        fprintf(temp, "%s\n", content);
        fprintf(temp, "echo\n");
        fprintf(temp, "read -p 'Press Enter to close...'\n");
        fclose(temp);
        chmod(tempFile, 0755);

        char command[1024];
        snprintf(command, sizeof(command),
                 "x-terminal-emulator -e \"%s\" 2>/dev/null || "
                 "gnome-terminal -- \"%s\" 2>/dev/null || "
                 "xterm -e \"%s\" 2>/dev/null || "
                 "konsole -e \"%s\" 2>/dev/null &",
                 tempFile, tempFile, tempFile, tempFile);
        system(command);
    }
#endif

    free(content);
}

// Open scripts folder in file explorer
void openScriptsFolder(const char *scriptDir) {
#ifdef PLATFORM_WINDOWS
    char command[1024];
    snprintf(command, sizeof(command), "explorer \"%s\"", scriptDir);
    system(command);
#else
    char command[1024];
    snprintf(command, sizeof(command),
             "xdg-open \"%s\" 2>/dev/null || "
             "nautilus \"%s\" 2>/dev/null || "
             "dolphin \"%s\" 2>/dev/null &",
             scriptDir, scriptDir, scriptDir);
    system(command);
#endif
}

// Save script
bool saveNewScript(const char *scriptDir, const char *filename, const char *command) {
    if (strlen(filename) == 0 || strlen(command) == 0) {
        printf("Filename or command is empty\n");
        return false;
    }

    char filepath[512];

#ifdef PLATFORM_WINDOWS
    if (strstr(filename, ".bat") == NULL && strstr(filename, ".cmd") == NULL) {
        snprintf(filepath, sizeof(filepath), "%s/%s.bat", scriptDir, filename);
    } else {
        snprintf(filepath, sizeof(filepath), "%s/%s", scriptDir, filename);
    }
#else
    if (strstr(filename, ".sh") == NULL) {
        snprintf(filepath, sizeof(filepath), "%s/%s.sh", scriptDir, filename);
    } else {
        snprintf(filepath, sizeof(filepath), "%s/%s", scriptDir, filename);
    }
#endif

    FILE *file = fopen(filepath, "w");
    if (file == NULL) {
        printf("Failed to create script file\n");
        return false;
    }

#ifdef PLATFORM_WINDOWS
    fprintf(file, "@echo off\n");
    fprintf(file, "%s\n", command);
#else
    fprintf(file, "#!/bin/bash\n");
    fprintf(file, "%s\n", command);
#endif

    fclose(file);

#ifndef PLATFORM_WINDOWS
    chmod(filepath, 0755);
#endif

    printf("Created script: %s\n", filepath);
    return true;
}

// Delete script file
bool deleteScript(const char *filepath) {
    if (remove(filepath) == 0) {
        printf("Deleted script: %s\n", filepath);
        return true;
    } else {
        printf("Failed to delete script: %s\n", filepath);
        return false;
    }
}

// Reload files from directory
int loadFiles(FileItem *files, const char *scriptDir) {
    int fileCount = 0;

    DIR *dir = opendir(scriptDir);
    if (dir == NULL) {
        printf("Failed to open scripts directory\n");
        return 0;
    }

    struct dirent *entry;

    while ((entry = readdir(dir)) && fileCount < MAX_FILES) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        snprintf(files[fileCount].filePath, sizeof(files[fileCount].filePath),
                 "%s/%s", scriptDir, entry->d_name);

        const char *ext = strrchr(entry->d_name, '.');
        if (ext != NULL) {
            strncpy(files[fileCount].fileExtension, ext, sizeof(files[fileCount].fileExtension) - 1);
            files[fileCount].fileExtension[sizeof(files[fileCount].fileExtension) - 1] = '\0';
        } else {
            files[fileCount].fileExtension[0] = '\0';
        }

        size_t len = strlen(entry->d_name);
        size_t dotIndex = len;
        for (size_t x = 0; x < len; x++) {
            if (entry->d_name[x] == '.') {
                dotIndex = x;
                break;
            }
        }

        strncpy(files[fileCount].displayName, entry->d_name, dotIndex);
        files[fileCount].displayName[dotIndex] = '\0';
        files[fileCount].isExecuting = false;

        fileCount++;
    }
    closedir(dir);

    return fileCount;
}

// Push command to undo stack
void pushUndo(Modal *modal) {
    if (modal->undoStackSize >= MAX_UNDO_STACK) {
        // Shift stack
        for (int i = 0; i < MAX_UNDO_STACK - 1; i++) {
            strcpy(modal->undoStack[i], modal->undoStack[i + 1]);
        }
        modal->undoStackSize = MAX_UNDO_STACK - 1;
    }

    strcpy(modal->undoStack[modal->undoStackSize], modal->command);
    modal->undoStackSize++;
    modal->redoStackSize = 0; // Clear redo stack on new action
}

// Perform undo
void performUndo(Modal *modal) {
    if (modal->undoStackSize > 0) {
        // Push current to redo stack
        if (modal->redoStackSize < MAX_UNDO_STACK) {
            strcpy(modal->redoStack[modal->redoStackSize], modal->command);
            modal->redoStackSize++;
        }

        // Pop from undo stack
        modal->undoStackSize--;
        strcpy(modal->command, modal->undoStack[modal->undoStackSize]);
        modal->commandLength = strlen(modal->command);
    }
}

// Perform redo
void performRedo(Modal *modal) {
    if (modal->redoStackSize > 0) {
        // Push current to undo stack
        pushUndo(modal);

        // Pop from redo stack
        modal->redoStackSize--;
        strcpy(modal->command, modal->redoStack[modal->redoStackSize]);
        modal->commandLength = strlen(modal->command);

        // Remove the auto-pushed undo (we just did that)
        modal->undoStackSize--;
    }
}

// Initialize modal
void initModal(Modal *modal) {
    modal->isOpen = false;
    modal->filename[0] = '\0';
    modal->command[0] = '\0';
    modal->filenameLength = 0;
    modal->commandLength = 0;
    modal->filenameActive = true;
    modal->commandActive = false;
    modal->framesCounter = 0;
    modal->isEditMode = false;
    modal->editIndex = -1;
    modal->commandScrollOffsetY = 0;
    modal->commandScrollOffsetX = 0;
    modal->commandMaxScrollY = 0;
    modal->commandMaxScrollX = 0;
    modal->undoStackSize = 0;
    modal->redoStackSize = 0;
}

// Open modal for new script
void openModal(Modal *modal) {
    modal->isOpen = true;
    modal->filename[0] = '\0';
    modal->command[0] = '\0';
    modal->filenameLength = 0;
    modal->commandLength = 0;
    modal->filenameActive = true;
    modal->commandActive = false;
    modal->framesCounter = 0;
    modal->isEditMode = false;
    modal->editIndex = -1;
    modal->commandScrollOffsetY = 0;
    modal->commandScrollOffsetX = 0;
    modal->commandMaxScrollY = 0;
    modal->commandMaxScrollX = 0;
    modal->undoStackSize = 0;
    modal->redoStackSize = 0;
}

// Open modal for editing
void openEditModal(Modal *modal, FileItem *file, int index) {
    modal->isOpen = true;
    modal->isEditMode = true;
    modal->editIndex = index;

    strncpy(modal->filename, file->displayName, MAX_FILENAME_CHARS);
    modal->filename[MAX_FILENAME_CHARS] = '\0';
    modal->filenameLength = strlen(modal->filename);

    char *content = readFileContent(file->filePath);
    if (content != NULL) {
        char *actualCommand = content;
#ifdef PLATFORM_WINDOWS
        if (strncmp(content, "@echo off", 9) == 0) {
            actualCommand = strchr(content, '\n');
            if (actualCommand) actualCommand++;
        }
#else
        if (strncmp(content, "#!/bin/bash", 11) == 0) {
            actualCommand = strchr(content, '\n');
            if (actualCommand) actualCommand++;
        }
#endif

        if (actualCommand) {
            strncpy(modal->command, actualCommand, MAX_COMMAND_CHARS);
            modal->command[MAX_COMMAND_CHARS] = '\0';
            int len = strlen(modal->command);
            while (len > 0 && (modal->command[len-1] == '\n' || modal->command[len-1] == '\r')) {
                modal->command[--len] = '\0';
            }
            modal->commandLength = len;
        }
        free(content);
    }

    modal->filenameActive = true;
    modal->commandActive = false;
    modal->framesCounter = 0;
    modal->commandScrollOffsetY = 0;
    modal->commandScrollOffsetX = 0;
    modal->commandMaxScrollY = 0;
    modal->commandMaxScrollX = 0;
    modal->undoStackSize = 0;
    modal->redoStackSize = 0;

    // Push initial state to undo stack
    if (modal->commandLength > 0) {
        pushUndo(modal);
    }
}

// Close modal
void closeModal(Modal *modal) {
    modal->isOpen = false;
}

// Draw a simple file icon
void drawFileIcon(int x, int y, Color color) {
    DrawRectangle(x, y + 3, 16, 20, color);
    DrawRectangle(x, y, 12, 3, color);
    DrawTriangle((Vector2){x + 12, y}, (Vector2){x + 16, y + 3}, (Vector2){x + 12, y + 3}, color);
    DrawRectangle(x + 3, y + 8, 10, 1, RAYWHITE);
    DrawRectangle(x + 3, y + 11, 10, 1, RAYWHITE);
    DrawRectangle(x + 3, y + 14, 7, 1, RAYWHITE);
}

// Draw folder icon
void drawFolderIcon(int x, int y) {
    DrawRectangle(x, y + 4, 18, 12, (Color){255, 200, 100, 255});
    DrawRectangle(x, y + 2, 8, 3, (Color){255, 200, 100, 255});
    DrawRectangleLines(x, y + 4, 18, 12, (Color){200, 150, 80, 255});
}

// Helper function to draw text with custom font
void DrawTextCustom(Font font, bool useFont, const char *text, int x, int y, int size, Color color) {
    if (useFont) {
        DrawTextEx(font, text, (Vector2){(float)x, (float)y}, (float)size, 1.0f, color);
    } else {
        DrawText(text, x, y, size, color);
    }
}

// Draw command text with word wrapping
void drawCommandWrapped(Font font, const char *text, int x, int y, int textSize, Rectangle bounds, float scrollY, float maxWidth, float *outMaxHeight) {
    int currentX = x;
    int currentY = y - (int)scrollY;
    int lineHeight = textSize + 4;
    int lineStartX = x;
    int totalHeight = lineHeight;

    char word[256] = {0};
    int wordLen = 0;

    for (int i = 0; i <= strlen(text); i++) {
        char c = text[i];

        if (c == '\n' || c == '\0' || c == ' ' || c == '\t') {
            // Draw accumulated word
            if (wordLen > 0) {
                word[wordLen] = '\0';
                Vector2 wordSize = MeasureTextEx(font, word, (float)textSize, 1.0f);

                // Check if word fits on current line
                if (currentX > lineStartX && currentX + wordSize.x > x + maxWidth) {
                    // Move to next line
                    currentY += lineHeight;
                    currentX = lineStartX;
                    totalHeight += lineHeight;
                }

                // Draw word if visible
                if (currentY >= bounds.y && currentY <= bounds.y + bounds.height) {
                    DrawTextEx(font, word, (Vector2){(float)currentX, (float)currentY}, (float)textSize, 1.0f, (Color){248, 248, 242, 255});
                }

                currentX += (int)wordSize.x;
                wordLen = 0;
            }

            // Handle special characters
            if (c == ' ') {
                Vector2 spaceSize = MeasureTextEx(font, " ", (float)textSize, 1.0f);
                currentX += (int)spaceSize.x;
            } else if (c == '\t') {
                Vector2 tabSize = MeasureTextEx(font, "    ", (float)textSize, 1.0f);
                currentX += (int)tabSize.x;
            } else if (c == '\n') {
                currentY += lineHeight;
                currentX = lineStartX;
                totalHeight += lineHeight;
            }
        } else {
            // Accumulate word
            if (wordLen < 255) {
                word[wordLen++] = c;
            }
        }
    }

    if (outMaxHeight) *outMaxHeight = (float)totalHeight;
}

// Draw command text with line numbers, scrolling, and clipping
void DrawCommandWithLineNumbers(Font font, bool useFont,
                                const char *text,
                                Rectangle box,
                                float scrollY,
                                int _fontSize) {

    BeginScissorMode(
        (int)box.x,
        (int)box.y,
        (int)box.width,
        (int)box.height
    );

    int lineHeight = _fontSize + 4; // spacing between lines
    float yOffset = -scrollY;
    int lineNumber = 1;

    // Make a modifiable copy of text
    char textCopy[MAX_COMMAND_CHARS + 1];
    strncpy(textCopy, text, MAX_COMMAND_CHARS);
    textCopy[MAX_COMMAND_CHARS] = '\0';

    // Split text by newline
    char *line = strtok(textCopy, "\n");
    while (line != NULL) {
        float lineY = box.y + yOffset;

        // Only draw lines that are visible
        if (lineY + lineHeight > box.y && lineY < box.y + box.height) {

            // Draw line number on the left
            DrawText(TextFormat("%d", lineNumber),
                     (int)(box.x + 5),
                     (int)lineY,
                     fontSize,
                     (Color){139, 233, 253, 255});

            // Draw actual command text shifted to the right of line numbers
            DrawTextEx(font,
                       line,
                       (Vector2){box.x + 35, lineY},
                       (float)fontSize,
                       1.0f,
                       (Color){248, 248, 242, 255});
        }

        yOffset += lineHeight;
        lineNumber++;
        line = strtok(NULL, "\n");
    }

    EndScissorMode();
};

int main() {
    FileItem files[MAX_FILES];
    int fileCount = 0;
    char scriptDir[512];
    getScriptsPath(scriptDir, sizeof(scriptDir));

    fileCount = loadFiles(files, scriptDir);

    #ifdef PLATFORM_WINDOWS
        printf("Running on Windows\n");
    #else
        printf("Running on Linux\n");
    #endif

    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(screenWidth, screenHeight, "k0rT Script Manager");
    SetTargetFPS(intialFPS);

    // Load custom font
    Font customFont = {0};
    bool useCustomFont = false;

    #ifdef PLATFORM_WINDOWS
        if (FileExists("fonts\\ttf\\JetBrainsMono-Light.ttf")) {
            customFont = LoadFont("fonts\\ttf\\JetBrainsMono-Bold.ttf");
            useCustomFont = true;
            printf("Custom font loaded successfully\n");
        } else {
            printf("Font not found: fonts\\ttf\\JetBrainsMono-Light.ttf\n");
        }
    #else
        if (FileExists("fonts/ttf/JetBrainsMono-Light.ttf")) {
            customFont = LoadFont("fonts/ttf/JetBrainsMono-Light.ttf");
            useCustomFont = true;
            printf("Custom font loaded successfully\n");
        } else {
            printf("Font not found: fonts/ttf/JetBrainsMono-Light.ttf\n");
        }
    #endif

    Rectangle addButton = { 10, 10, 40, 40 };
    Rectangle folderButton = { 60, 10, 40, 40 };

    ScrollableList scrollList;
    scrollList.container = (Rectangle){ 20, 70, GetScreenWidth() - 40, GetScreenHeight() - 130 };
    scrollList.scrollOffset = 0;
    scrollList.maxScroll = 0;

    Modal modal;
    initModal(&modal);

    int executingIndex = -1;
    Image icon = LoadImage("icons/logo.png");
    SetWindowIcon(icon);

    while (!WindowShouldClose()) {
        Vector2 mousePoint = GetMousePosition();

        scrollList.container.width = GetScreenWidth() - 40;
        scrollList.container.height = GetScreenHeight() - 130;

        int contentHeight = fileCount * 40;
        scrollList.maxScroll = contentHeight - scrollList.container.height;
        if (scrollList.maxScroll < 0) scrollList.maxScroll = 0;

        if (modal.isOpen) {
            modal.framesCounter++;

            // Handle vertical scrolling in command box only
            if (CheckCollisionPointRec(mousePoint, modal.commandBox)) {
                float wheel = GetMouseWheelMove();
                if (wheel != 0) {
                    modal.commandScrollOffsetY -= wheel * 20;
                }
            }

            // Mouse click to switch fields
            if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                if (CheckCollisionPointRec(mousePoint, modal.filenameBox)) {
                    modal.filenameActive = true;
                    modal.commandActive = false;
                } else if (CheckCollisionPointRec(mousePoint, modal.commandBox)) {
                    modal.filenameActive = false;
                    modal.commandActive = true;
                }
            }

            // Handle text input for filename
            if (modal.filenameActive) {
                int key = GetCharPressed();
                while (key > 0) {
                    if ((key >= 32) && (key <= 126) && (modal.filenameLength < MAX_FILENAME_CHARS)) {
                        modal.filename[modal.filenameLength] = (char)key;
                        modal.filenameLength++;
                        modal.filename[modal.filenameLength] = '\0';
                    }
                    key = GetCharPressed();
                }

                if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) {
                    if (modal.filenameLength > 0) {
                        modal.filenameLength--;
                        modal.filename[modal.filenameLength] = '\0';
                    }
                }

                // Clipboard support
                if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) {
                    if (IsKeyPressed(KEY_V)) {
                        const char *clipText = GetClipboardText();
                        if (clipText != NULL) {
                            int clipLen = strlen(clipText);
                            for (int i = 0; i < clipLen && modal.filenameLength < MAX_FILENAME_CHARS; i++) {
                                if (clipText[i] >= 32 && clipText[i] <= 126 && clipText[i] != '\n' && clipText[i] != '\r') {
                                    modal.filename[modal.filenameLength++] = clipText[i];
                                }
                            }
                            modal.filename[modal.filenameLength] = '\0';
                        }
                    } else if (IsKeyPressed(KEY_C)) {
                        if (modal.filenameLength > 0) {
                            SetClipboardText(modal.filename);
                        }
                    } else if (IsKeyPressed(KEY_X)) {
                        if (modal.filenameLength > 0) {
                            SetClipboardText(modal.filename);
                            modal.filename[0] = '\0';
                            modal.filenameLength = 0;
                        }
                    } else if (IsKeyPressed(KEY_A)) {
                        // Select all - just copy for now
                        if (modal.filenameLength > 0) {
                            SetClipboardText(modal.filename);
                        }
                    }
                }
            }

            // Handle text input for command (multi-line)
            if (modal.commandActive) {
                // Undo/Redo
                if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) {
                    if (IsKeyPressed(KEY_Z)) {
                        performUndo(&modal);
                    } else if (IsKeyPressed(KEY_Y) || (IsKeyDown(KEY_LEFT_SHIFT) && IsKeyPressed(KEY_Z))) {
                        performRedo(&modal);
                    }
                }

                bool textChanged = false;
                int key = GetCharPressed();
                while (key > 0) {
                    if ((key >= 32) && (key <= 126) && (modal.commandLength < MAX_COMMAND_CHARS)) {
                        if (!textChanged) {
                            pushUndo(&modal);
                            textChanged = true;
                        }
                        modal.command[modal.commandLength] = (char)key;
                        modal.commandLength++;
                        modal.command[modal.commandLength] = '\0';
                    }
                    key = GetCharPressed();
                }

                // Handle Enter for new line
                if (IsKeyPressed(KEY_ENTER)) {
                    if (modal.commandLength < MAX_COMMAND_CHARS) {
                        if (!textChanged) {
                            pushUndo(&modal);
                            textChanged = true;
                        }
                        modal.command[modal.commandLength] = '\n';
                        modal.commandLength++;
                        modal.command[modal.commandLength] = '\0';
                    }
                }

                if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) {
                    if (modal.commandLength > 0) {
                        if (!textChanged) {
                            pushUndo(&modal);
                            textChanged = true;
                        }
                        modal.commandLength--;
                        modal.command[modal.commandLength] = '\0';
                    }
                }

                // Clipboard support
                if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) {
                    if (IsKeyPressed(KEY_V)) {
                        const char *clipText = GetClipboardText();
                        if (clipText != NULL) {
                            pushUndo(&modal);
                            int clipLen = strlen(clipText);
                            for (int i = 0; i < clipLen && modal.commandLength < MAX_COMMAND_CHARS; i++) {
                                if ((clipText[i] >= 32 && clipText[i] <= 126) || clipText[i] == '\n' || clipText[i] == '\t') {
                                    modal.command[modal.commandLength++] = clipText[i];
                                }
                            }
                            modal.command[modal.commandLength] = '\0';
                        }
                    } else if (IsKeyPressed(KEY_C)) {
                        if (modal.commandLength > 0) {
                            SetClipboardText(modal.command);
                        }
                    } else if (IsKeyPressed(KEY_X)) {
                        if (modal.commandLength > 0) {
                            SetClipboardText(modal.command);
                            pushUndo(&modal);
                            modal.command[0] = '\0';
                            modal.commandLength = 0;
                        }
                    } else if (IsKeyPressed(KEY_A)) {
                        // Select all - just copy for now
                        if (modal.commandLength > 0) {
                            SetClipboardText(modal.command);
                        }
                    }
                }
            }

            if (IsKeyPressed(KEY_TAB)) {
                modal.filenameActive = !modal.filenameActive;
                modal.commandActive = !modal.commandActive;
            }
        } else {
            // Handle scrolling
            if (CheckCollisionPointRec(mousePoint, scrollList.container)) {
                float wheel = GetMouseWheelMove();
                scrollList.scrollOffset -= wheel * 20;
                if (scrollList.scrollOffset < 0) scrollList.scrollOffset = 0;
                if (scrollList.scrollOffset > scrollList.maxScroll) scrollList.scrollOffset = scrollList.maxScroll;
            }

            // Check for "+" button click
            if (CheckCollisionPointRec(mousePoint, addButton)) {
                if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                    openModal(&modal);
                }
            }

            // Check for folder button click
            if (CheckCollisionPointRec(mousePoint, folderButton)) {
                if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                    openScriptsFolder(scriptDir);
                }
            }

            // Update file bounds and check for clicks
            float y = scrollList.container.y + 10 - scrollList.scrollOffset;
            float x = scrollList.container.x + 10;

            for (int i = 0; i < fileCount; i++) {
                files[i].bounds = (Rectangle){ x + 30, y, (float)MeasureText(files[i].displayName, fontSize), (float)fontSize };
                files[i].editBounds = (Rectangle){
                    scrollList.container.x + scrollList.container.width - 80,
                    y - 2, 30, 24
                };
                files[i].deleteBounds = (Rectangle){
                    scrollList.container.x + scrollList.container.width - 40,
                    y - 2, 30, 24
                };

                if (y >= scrollList.container.y && y <= scrollList.container.y + scrollList.container.height) {
                    if (CheckCollisionPointRec(mousePoint, files[i].bounds)) {
                        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                            executeFileContent(files[i].filePath);
                            files[i].isExecuting = true;
                            executingIndex = i;
                        }
                    }

                    if (CheckCollisionPointRec(mousePoint, files[i].editBounds)) {
                        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                            openEditModal(&modal, &files[i], i);
                        }
                    }

                    if (CheckCollisionPointRec(mousePoint, files[i].deleteBounds)) {
                        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                            if (deleteScript(files[i].filePath)) {
                                fileCount = loadFiles(files, scriptDir);
                            }
                        }
                    }
                }

                y += 40;
            }

            static int executingTimer = 0;
            if (executingIndex >= 0) {
                executingTimer++;
                if (executingTimer > 30) {
                    if (executingIndex < fileCount) {
                        files[executingIndex].isExecuting = false;
                    }
                    executingIndex = -1;
                    executingTimer = 0;
                }
            }
        }

        // Draw
        BeginDrawing();
        ClearBackground((Color){40, 42, 54, 255});

        if (!modal.isOpen) {
            // Draw "+" button
            Color buttonColor = CheckCollisionPointRec(mousePoint, addButton) ?
                               (Color){70, 75, 90, 255} : (Color){50, 55, 70, 255};
            DrawRectangleRec(addButton, buttonColor);
            DrawRectangleLinesEx(addButton, 2, (Color){100, 105, 120, 255});
            DrawTextCustom(customFont, useCustomFont, "+", (int)addButton.x + 12, (int)addButton.y + 6, 32, (Color){248, 248, 242, 255});

            // Draw folder button
            Color folderColor = CheckCollisionPointRec(mousePoint, folderButton) ?
                               (Color){70, 75, 90, 255} : (Color){50, 55, 70, 255};
            DrawRectangleRec(folderButton, folderColor);
            DrawRectangleLinesEx(folderButton, 2, (Color){100, 105, 120, 255});
            drawFolderIcon((int)folderButton.x + 11, (int)folderButton.y + 11);

            DrawTextCustom(customFont, useCustomFont, "Add Script", 110, 16, 16, (Color){189, 147, 249, 255});
            DrawTextCustom(customFont, useCustomFont, "| Open Folder", 190, 16, 16, (Color){139, 233, 253, 255});

            // Draw scrollable container
            DrawRectangleRec(scrollList.container, (Color){30, 32, 44, 255});
            DrawRectangleLinesEx(scrollList.container, 2, (Color){68, 71, 90, 255});

            BeginScissorMode(
                (int)scrollList.container.x,
                (int)scrollList.container.y,
                (int)scrollList.container.width,
                (int)scrollList.container.height
            );

            float y = scrollList.container.y + 10 - scrollList.scrollOffset;
            float x = scrollList.container.x + 10;

            for (int i = 0; i < fileCount; i++) {
                if (y >= scrollList.container.y - 40 && y <= scrollList.container.y + scrollList.container.height) {
                    Color textColor = (Color){248, 248, 242, 255};
                    Color iconColor = (Color){189, 147, 249, 255};

                    if (files[i].isExecuting) {
                        textColor = (Color){80, 250, 123, 255};
                        iconColor = (Color){80, 250, 123, 255};
                    }

                    if (CheckCollisionPointRec(mousePoint, files[i].bounds)) {
                        DrawRectangle((int)x, (int)(y - 5), (int)scrollList.container.width - 20, 30, (Color){44, 47, 62, 255});
                    }

                    drawFileIcon((int)x, (int)y, iconColor);
                    DrawTextCustom(customFont, useCustomFont, files[i].displayName, (int)(x + 30), (int)y, fontSize, textColor);

                    Color editColor = CheckCollisionPointRec(mousePoint, files[i].editBounds) ?
                                     (Color){241, 250, 140, 255} : (Color){139, 233, 253, 255};
                    DrawRectangleRec(files[i].editBounds, editColor);
                    DrawRectangleLinesEx(files[i].editBounds, 1, (Color){98, 114, 164, 255});
                    DrawTextCustom(customFont, useCustomFont, "E", (int)files[i].editBounds.x + 10, (int)files[i].editBounds.y + 4, 16, (Color){40, 42, 54, 255});

                    Color deleteColor = CheckCollisionPointRec(mousePoint, files[i].deleteBounds) ?
                                       (Color){255, 85, 85, 255} : (Color){255, 121, 198, 255};
                    DrawRectangleRec(files[i].deleteBounds, deleteColor);
                    DrawRectangleLinesEx(files[i].deleteBounds, 1, (Color){98, 114, 164, 255});
                    DrawTextCustom(customFont, useCustomFont, "X", (int)files[i].deleteBounds.x + 10, (int)files[i].deleteBounds.y + 4, 16, WHITE);
                }

                y += 40;
            }

            EndScissorMode();

            if (scrollList.maxScroll > 0) {
                float scrollbarHeight = (scrollList.container.height / contentHeight) * scrollList.container.height;
                float scrollbarY = scrollList.container.y + (scrollList.scrollOffset / scrollList.maxScroll) * (scrollList.container.height - scrollbarHeight);

                DrawRectangle(
                    (int)(scrollList.container.x + scrollList.container.width - 8),
                    (int)scrollbarY,
                    6,
                    (int)scrollbarHeight,
                    (Color){98, 114, 164, 255}
                );
            }

            #ifdef PLATFORM_WINDOWS
                DrawTextCustom(customFont, useCustomFont, TextFormat("Windows | Scripts: %d | Format: .bat", fileCount),
                        10, GetScreenHeight() - 28, 16, (Color){98, 114, 164, 255});
            #else
                DrawTextCustom(customFont, useCustomFont, TextFormat("Linux | Scripts: %d | Format: .sh", fileCount),
                        10, GetScreenHeight() - 28, 16, (Color){98, 114, 164, 255});
            #endif
        } else {
            // Draw modal
            DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), (Color){0, 0, 0, 180});

            int modalWidth = 800;
            int modalHeight = 550;
            int modalX = (GetScreenWidth() - modalWidth) / 2;
            int modalY = (GetScreenHeight() - modalHeight) / 2;

            Rectangle modalBox = { (float)modalX, (float)modalY, (float)modalWidth, (float)modalHeight };
            DrawRectangleRec(modalBox, (Color){40, 42, 54, 255});
            DrawRectangleLinesEx(modalBox, 3, (Color){98, 114, 164, 255});

            const char *title = modal.isEditMode ? "Edit Script" : "Create New Script";
            DrawTextCustom(customFont, useCustomFont, title, modalX + 20, modalY + 20, 24, (Color){248, 248, 242, 255});

            // Filename input
            DrawTextCustom(customFont, useCustomFont, "Filename:", modalX + 20, modalY + 60, 18, (Color){189, 147, 249, 255});
            modal.filenameBox = (Rectangle){ (float)(modalX + 20), (float)(modalY + 85), (float)(modalWidth - 40), 35 };
            DrawRectangleRec(modal.filenameBox, (Color){68, 71, 90, 255});
            DrawRectangleLinesEx(modal.filenameBox, 2, modal.filenameActive ? (Color){139, 233, 253, 255} : (Color){98, 114, 164, 255});
            DrawTextCustom(customFont, useCustomFont, modal.filename, modalX + 25, modalY + 93, 20, (Color){248, 248, 242, 255});

            if (modal.filenameActive && ((modal.framesCounter / 20) % 2) == 0) {
                int cursorPos = modalX + 25;
                if (useCustomFont) {
                    Vector2 size = MeasureTextEx(customFont, modal.filename, 20, 1.0f);
                    cursorPos += (int)size.x;
                } else {
                    cursorPos += MeasureText(modal.filename, 20);
                }
                DrawTextCustom(customFont, useCustomFont, "_", cursorPos, modalY + 93, 20, (Color){248, 248, 242, 255});
            }

            // Command input with line numbers
            DrawTextCustom(customFont, useCustomFont, "Command (Press Enter for new line):", modalX + 20, modalY + 135, 18, (Color){189, 147, 249, 255});
            modal.commandBox = (Rectangle){ (float)(modalX + 20), (float)(modalY + 160), (float)(modalWidth - 40), 300 };
            DrawRectangleRec(modal.commandBox, (Color){68, 71, 90, 255});
            DrawRectangleLinesEx(modal.commandBox, 2, modal.commandActive ? (Color){139, 233, 253, 255} : (Color){98, 114, 164, 255});

            // Draw line numbers background
            DrawRectangle(modalX + 20, modalY + 160, 40, 300, (Color){50, 52, 64, 255});
            DrawLine(modalX + 60, modalY + 160, modalX + 60, modalY + 460, (Color){98, 114, 164, 255});

            // Count lines for wrapped text
            int lineCount = 1;
            for (int i = 0; i < modal.commandLength; i++) {
                if (modal.command[i] == '\n') lineCount++;
            }

            // Draw line numbers
            for (int i = 1; i <= lineCount; i++) {
                if (useCustomFont) {
                    DrawTextEx(customFont, TextFormat("%d", i), (Vector2){(float)(modalX + 25), (float)(modalY + 165 + (i-1) * 20)}, 14, 1.0f, (Color){98, 114, 164, 255});
                } else {
                    DrawText(TextFormat("%d", i), modalX + 25, modalY + 165 + (i-1) * 20, 14, (Color){98, 114, 164, 255});
                }
            }

            // Draw command text with wrapping
            BeginScissorMode(modalX + 65, modalY + 165, modalWidth - 100, 500);

            float contentHeight = 0;
            float maxWidth = (float)(modalWidth - 100);

            if (useCustomFont) {
                drawCommandWrapped(customFont, modal.command, modalX + 65, modalY + 165, 16, modal.commandBox,
                               modal.commandScrollOffsetY, maxWidth, &contentHeight);
            } else {
                // Fallback if font not loaded - just draw plain
                DrawText("Font not loaded - please check fonts folder", modalX + 65, modalY + 165, 14, RED);
            }

            // Calculate max scroll (only vertical)
            modal.commandMaxScrollY = contentHeight - 290;
            if (modal.commandMaxScrollY < 0) modal.commandMaxScrollY = 0;

            // Clamp scroll offset
            if (modal.commandScrollOffsetY < 0) modal.commandScrollOffsetY = 0;
            if (modal.commandScrollOffsetY > modal.commandMaxScrollY) modal.commandScrollOffsetY = modal.commandMaxScrollY;

            // Draw cursor
            if (modal.commandActive && ((modal.framesCounter / 20) % 2) == 0) {
                int cursorY = modalY + 165 - (int)modal.commandScrollOffsetY;
                int cursorX = modalX + 65;

                // Count newlines to get cursor Y position
                for (int i = 0; i < modal.commandLength; i++) {
                    if (modal.command[i] == '\n') cursorY += 20;
                }

                // Get last line for X position
                int lastLineStart = modal.commandLength;
                for (int i = modal.commandLength - 1; i >= 0; i--) {
                    if (modal.command[i] == '\n') {
                        lastLineStart = i + 1;
                        break;
                    }
                    if (i == 0) lastLineStart = 0;
                }

                char lastLine[MAX_COMMAND_CHARS];
                int lastLineLen = modal.commandLength - lastLineStart;
                strncpy(lastLine, modal.command + lastLineStart, lastLineLen);
                lastLine[lastLineLen] = '\0';

                if (useCustomFont) {
                    Vector2 lastLineSize = MeasureTextEx(customFont, lastLine, 16, 1.0f);
                    cursorX += (int)lastLineSize.x;
                } else {
                    cursorX += MeasureText(lastLine, 16);
                }

                // Only draw cursor if visible in viewport
                if (cursorY >= modalY + 165 && cursorY <= modalY + 455 &&
                    cursorX >= modalX + 65 && cursorX <= modalX + modalWidth - 35) {
                    if (useCustomFont) {
                        DrawTextEx(customFont, "_", (Vector2){(float)cursorX, (float)cursorY}, 16, 1.0f, (Color){248, 248, 242, 255});
                    } else {
                        DrawText("_", cursorX, cursorY, 16, (Color){248, 248, 242, 255});
                    }
                }
            }

            EndScissorMode();

            // Draw vertical scrollbar only
            if (modal.commandMaxScrollY > 0) {
                float scrollbarHeight = (290 / contentHeight) * 290;
                if (scrollbarHeight < 20) scrollbarHeight = 20;
                float scrollbarY = modalY + 165 + (modal.commandScrollOffsetY / modal.commandMaxScrollY) * (290 - scrollbarHeight);

                DrawRectangle(modalX + modalWidth - 30, (int)scrollbarY, 6, (int)scrollbarHeight, (Color){98, 114, 164, 255});
            }

            // Buttons
            Rectangle saveButton = { (float)(modalX + modalWidth - 220), (float)(modalY + modalHeight - 50), 90, 35 };
            Rectangle cancelButton = { (float)(modalX + modalWidth - 120), (float)(modalY + modalHeight - 50), 90, 35 };

            Color saveColor = CheckCollisionPointRec(mousePoint, saveButton) ?
                             (Color){80, 250, 123, 255} : (Color){50, 200, 93, 255};
            DrawRectangleRec(saveButton, saveColor);
            DrawRectangleLinesEx(saveButton, 2, (Color){40, 150, 73, 255});
            DrawTextCustom(customFont, useCustomFont, "Save", (int)saveButton.x + 25, (int)saveButton.y + 8, 20, (Color){40, 42, 54, 255});

            if (CheckCollisionPointRec(mousePoint, saveButton) && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                if (modal.isEditMode && modal.editIndex >= 0 && modal.editIndex < fileCount) {
                    deleteScript(files[modal.editIndex].filePath);
                }

                if (saveNewScript(scriptDir, modal.filename, modal.command)) {
                    fileCount = loadFiles(files, scriptDir);
                    closeModal(&modal);
                }
            }

            Color cancelColor = CheckCollisionPointRec(mousePoint, cancelButton) ?
                               (Color){255, 85, 85, 255} : (Color){255, 121, 198, 255};
            DrawRectangleRec(cancelButton, cancelColor);
            DrawRectangleLinesEx(cancelButton, 2, (Color){200, 80, 140, 255});
            DrawTextCustom(customFont, useCustomFont, "Cancel", (int)cancelButton.x + 15, (int)cancelButton.y + 8, 20, (Color){40, 42, 54, 255});

            if (CheckCollisionPointRec(mousePoint, cancelButton) && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                closeModal(&modal);
            }

            DrawTextCustom(customFont, useCustomFont, "TAB: switch | ENTER: new line | CTRL+Z: undo ", modalX + 20, modalY + modalHeight - 50, 13, (Color){98, 114, 164, 255});
            DrawTextCustom(customFont, useCustomFont, "CTRL+Y: redo | CTRL+C/V/X: copy/paste/cut", modalX+20 , modalY + modalHeight - 30, 13, (Color){98, 114, 164, 255});
        }

        EndDrawing();
    }

    // Unload custom font
    if (useCustomFont) {
        UnloadFont(customFont);
    }

    CloseWindow();
    return 0;
}

/* #include "raylib.h" */
/* #include <stdio.h> */
/* #include <string.h> */
/* #include <dirent.h> */
/* #include <stdlib.h> */

/* // Platform detection */
/* #ifdef _WIN32 */
/*     #define PLATFORM_WINDOWS */
/*     #define WIN32_LEAN_AND_MEAN */
/*     #define NOGDI */
/*     #define NOUSER */
/*     #include <windows.h> */
/*     #undef DrawText */
/*     #undef CloseWindow */
/*     #undef ShowCursor */
/* #else */
/*     #define PLATFORM_LINUX */
/* #endif */

/* #define MAX_INPUT_CHARS 100 */
/* #define screenWidth 800 */
/* #define screenHeight 500 */
/* #define intialFPS 60 */
/* #define MAX_FILES 256 */
/* #define fontSize 20 */
/* #define MAX_SCRIPT_SIZE 65536 */
/* #define MAX_FILENAME_CHARS 50 */
/* #define MAX_COMMAND_CHARS 500 */

/* typedef struct { */
/*     char displayName[256]; */
/*     char filePath[512]; */
/*     char fileExtension[16]; */
/*     Rectangle bounds; */
/*     Rectangle editBounds; */
/*     Rectangle deleteBounds; */
/*     bool isExecuting; */
/* } FileItem; */

/* typedef struct { */
/*     bool isOpen; */
/*     char filename[MAX_FILENAME_CHARS + 1]; */
/*     char command[MAX_COMMAND_CHARS + 1]; */
/*     int filenameLength; */
/*     int commandLength; */
/*     bool filenameActive; */
/*     bool commandActive; */
/*     int framesCounter; */
/*     bool isEditMode; */
/*     int editIndex; */
/*     Rectangle filenameBox; */
/*     Rectangle commandBox; */
/* } Modal; */

/* typedef struct { */
/*     Rectangle container; */
/*     float scrollOffset; */
/*     float maxScroll; */
/* } ScrollableList; */

/* // Read file content */
/* char* readFileContent(const char *filepath) { */
/*     FILE *file = fopen(filepath, "r"); */
/*     if (file == NULL) { */
/*         printf("Failed to open file: %s\n", filepath); */
/*         return NULL; */
/*     } */

/*     char *buffer = (char*)malloc(MAX_SCRIPT_SIZE); */
/*     if (buffer == NULL) { */
/*         fclose(file); */
/*         return NULL; */
/*     } */

/*     size_t bytesRead = fread(buffer, 1, MAX_SCRIPT_SIZE - 1, file); */
/*     buffer[bytesRead] = '\0'; */
/*     fclose(file); */
/*     return buffer; */
/* } */

/* // Execute file content as command using system() */
/* void executeFileContent(const char *filepath) { */
/*     printf("Executing commands from: %s\n", filepath); */

/*     char *content = readFileContent(filepath); */
/*     if (content == NULL) { */
/*         printf("Failed to read file content\n"); */
/*         return; */
/*     } */

/*     printf("Command content:\n%s\n", content); */

/* #ifdef PLATFORM_WINDOWS */
/*     // Fixed: Use larger buffer to avoid truncation warning */
/*     char tempFile[1024]; */
/*     char tempPath[1024]; */
/*     GetTempPathA(sizeof(tempPath), tempPath); */
/*     snprintf(tempFile, sizeof(tempFile), "%s\\kort_exec.bat", tempPath); */

/*     FILE *temp = fopen(tempFile, "w"); */
/*     if (temp) { */
/*         fprintf(temp, "@echo off\n"); */
/*         fprintf(temp, "%s\n", content); */
/*         fprintf(temp, "echo.\n"); */
/*         fprintf(temp, "echo Press any key to close...\n"); */
/*         fprintf(temp, "pause > nul\n"); */
/*         fclose(temp); */

/*         char command[2048]; */
/*         snprintf(command, sizeof(command), "start cmd /c \"%s\"", tempFile); */
/*         system(command); */
/*     } */
/* #else */
/*     char tempFile[512]; */
/*     snprintf(tempFile, sizeof(tempFile), "/tmp/kort_exec.sh"); */

/*     FILE *temp = fopen(tempFile, "w"); */
/*     if (temp) { */
/*         fprintf(temp, "#!/bin/bash\n"); */
/*         fprintf(temp, "%s\n", content); */
/*         fprintf(temp, "echo\n"); */
/*         fprintf(temp, "read -p 'Press Enter to close...'\n"); */
/*         fclose(temp); */
/*         chmod(tempFile, 0755); */

/*         char command[1024]; */
/*         snprintf(command, sizeof(command),  */
/*                  "x-terminal-emulator -e \"%s\" 2>/dev/null || " */
/*                  "gnome-terminal -- \"%s\" 2>/dev/null || " */
/*                  "xterm -e \"%s\" 2>/dev/null || " */
/*                  "konsole -e \"%s\" 2>/dev/null &", */
/*                  tempFile, tempFile, tempFile, tempFile); */
/*         system(command); */
/*     } */
/* #endif */

/*     free(content); */
/* } */

/* // Save script - .bat for Windows, .sh for Linux */
/* bool saveNewScript(const char *scriptDir, const char *filename, const char *command) { */
/*     if (strlen(filename) == 0 || strlen(command) == 0) { */
/*         printf("Filename or command is empty\n"); */
/*         return false; */
/*     } */

/*     char filepath[512]; */

/* #ifdef PLATFORM_WINDOWS */
/*     // Use .bat for Windows scripts (standard for batch files) */
/*     if (strstr(filename, ".bat") == NULL && strstr(filename, ".cmd") == NULL) { */
/*         snprintf(filepath, sizeof(filepath), "%s/%s.bat", scriptDir, filename); */
/*     } else { */
/*         snprintf(filepath, sizeof(filepath), "%s/%s", scriptDir, filename); */
/*     } */
/* #else */
/*     // Use .sh for Linux shell scripts (standard for bash scripts) */
/*     if (strstr(filename, ".sh") == NULL) { */
/*         snprintf(filepath, sizeof(filepath), "%s/%s.sh", scriptDir, filename); */
/*     } else { */
/*         snprintf(filepath, sizeof(filepath), "%s/%s", scriptDir, filename); */
/*     } */
/* #endif */

/*     FILE *file = fopen(filepath, "w"); */
/*     if (file == NULL) { */
/*         printf("Failed to create script file\n"); */
/*         return false; */
/*     } */

/* #ifdef PLATFORM_WINDOWS */
/*     // Standard batch file header */
/*     fprintf(file, "@echo off\n"); */
/*     fprintf(file, "%s\n", command); */
/* #else */
/*     // Standard shell script header with shebang */
/*     fprintf(file, "#!/bin/bash\n"); */
/*     fprintf(file, "%s\n", command); */
/* #endif */

/*     fclose(file); */

/* #ifndef PLATFORM_WINDOWS */
/*     // Make script executable on Linux */
/*     chmod(filepath, 0755); */
/* #endif */

/*     printf("Created script: %s\n", filepath); */
/*     return true; */
/* } */

/* // Delete script file */
/* bool deleteScript(const char *filepath) { */
/*     if (remove(filepath) == 0) { */
/*         printf("Deleted script: %s\n", filepath); */
/*         return true; */
/*     } else { */
/*         printf("Failed to delete script: %s\n", filepath); */
/*         return false; */
/*     } */
/* } */

/* // Reload files from directory */
/* int loadFiles(FileItem *files, const char *scriptDir) { */
/*     int fileCount = 0; */

/*     DIR *dir = opendir(scriptDir); */
/*     if (dir == NULL) { */
/*         printf("Failed to open scripts directory\n"); */
/*         return 0; */
/*     } */

/*     struct dirent *entry; */

/*     while ((entry = readdir(dir)) && fileCount < MAX_FILES) { */
/*         if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)  */
/*             continue; */

/*         snprintf(files[fileCount].filePath, sizeof(files[fileCount].filePath),  */
/*                  "%s/%s", scriptDir, entry->d_name); */

/*         const char *ext = strrchr(entry->d_name, '.'); */
/*         if (ext != NULL) { */
/*             strncpy(files[fileCount].fileExtension, ext, sizeof(files[fileCount].fileExtension) - 1); */
/*             files[fileCount].fileExtension[sizeof(files[fileCount].fileExtension) - 1] = '\0'; */
/*         } else { */
/*             files[fileCount].fileExtension[0] = '\0'; */
/*         } */

/*         size_t len = strlen(entry->d_name); */
/*         size_t dotIndex = len; */
/*         for (size_t x = 0; x < len; x++) { */
/*             if (entry->d_name[x] == '.') {  */
/*                 dotIndex = x; */
/*                 break;  */
/*             } */
/*         } */

/*         strncpy(files[fileCount].displayName, entry->d_name, dotIndex); */
/*         files[fileCount].displayName[dotIndex] = '\0'; */
/*         files[fileCount].isExecuting = false; */

/*         fileCount++; */
/*     } */
/*     closedir(dir); */

/*     return fileCount; */
/* } */

/* // Initialize modal */
/* void initModal(Modal *modal) { */
/*     modal->isOpen = false; */
/*     modal->filename[0] = '\0'; */
/*     modal->command[0] = '\0'; */
/*     modal->filenameLength = 0; */
/*     modal->commandLength = 0; */
/*     modal->filenameActive = true; */
/*     modal->commandActive = false; */
/*     modal->framesCounter = 0; */
/*     modal->isEditMode = false; */
/*     modal->editIndex = -1; */
/* } */

/* // Open modal for new script */
/* void openModal(Modal *modal) { */
/*     modal->isOpen = true; */
/*     modal->filename[0] = '\0'; */
/*     modal->command[0] = '\0'; */
/*     modal->filenameLength = 0; */
/*     modal->commandLength = 0; */
/*     modal->filenameActive = true; */
/*     modal->commandActive = false; */
/*     modal->framesCounter = 0; */
/*     modal->isEditMode = false; */
/*     modal->editIndex = -1; */
/* } */

/* // Open modal for editing */
/* void openEditModal(Modal *modal, FileItem *file, int index) { */
/*     modal->isOpen = true; */
/*     modal->isEditMode = true; */
/*     modal->editIndex = index; */

/*     // Load filename */
/*     strncpy(modal->filename, file->displayName, MAX_FILENAME_CHARS); */
/*     modal->filename[MAX_FILENAME_CHARS] = '\0'; */
/*     modal->filenameLength = strlen(modal->filename); */

/*     // Load command from file */
/*     char *content = readFileContent(file->filePath); */
/*     if (content != NULL) { */
/*         // Skip shebang/header lines */
/*         char *actualCommand = content; */
/* #ifdef PLATFORM_WINDOWS */
/*         if (strncmp(content, "@echo off", 9) == 0) { */
/*             actualCommand = strchr(content, '\n'); */
/*             if (actualCommand) actualCommand++; */
/*         } */
/* #else */
/*         if (strncmp(content, "#!/bin/bash", 11) == 0) { */
/*             actualCommand = strchr(content, '\n'); */
/*             if (actualCommand) actualCommand++; */
/*         } */
/* #endif */

/*         if (actualCommand) { */
/*             strncpy(modal->command, actualCommand, MAX_COMMAND_CHARS); */
/*             modal->command[MAX_COMMAND_CHARS] = '\0'; */
/*             // Remove trailing newlines */
/*             int len = strlen(modal->command); */
/*             while (len > 0 && (modal->command[len-1] == '\n' || modal->command[len-1] == '\r')) { */
/*                 modal->command[--len] = '\0'; */
/*             } */
/*             modal->commandLength = len; */
/*         } */
/*         free(content); */
/*     } */

/*     modal->filenameActive = true; */
/*     modal->commandActive = false; */
/*     modal->framesCounter = 0; */
/* } */

/* // Close modal */
/* void closeModal(Modal *modal) { */
/*     modal->isOpen = false; */
/* } */

/* // Draw a simple file icon */
/* void drawFileIcon(int x, int y, Color color) { */
/*     // Document shape */
/*     DrawRectangle(x, y + 3, 16, 20, color); */
/*     DrawRectangle(x, y, 12, 3, color); */
/*     DrawTriangle((Vector2){x + 12, y}, (Vector2){x + 16, y + 3}, (Vector2){x + 12, y + 3}, color); */

/*     // Lines inside */
/*     DrawRectangle(x + 3, y + 8, 10, 1, RAYWHITE); */
/*     DrawRectangle(x + 3, y + 11, 10, 1, RAYWHITE); */
/*     DrawRectangle(x + 3, y + 14, 7, 1, RAYWHITE); */
/* } */

/* int main() { */
/*     FileItem files[MAX_FILES]; */
/*     int fileCount = 0; */

/*     const char *scriptDir = "./scripts"; */

/*     // Load initial files */
/*     fileCount = loadFiles(files, scriptDir); */

/*     #ifdef PLATFORM_WINDOWS */
/*         printf("Running on Windows\n"); */
/*     #else */
/*         printf("Running on Linux\n"); */
/*     #endif */

/*     SetConfigFlags(FLAG_WINDOW_RESIZABLE); */
/*     InitWindow(screenWidth, screenHeight, "k0rT Script Manager"); */
/*     SetTargetFPS(intialFPS); */

/*     // Load a nicer font (using raylib's default with better spacing) */
/*     Font font = GetFontDefault(); */

/*     // "+" button properties */
/*     Rectangle addButton = { 10, 10, 40, 40 }; */

/*     // Scrollable list container */
/*     ScrollableList scrollList; */
/*     scrollList.container = (Rectangle){ 20, 70, GetScreenWidth() - 40, GetScreenHeight() - 130 }; */
/*     scrollList.scrollOffset = 0; */
/*     scrollList.maxScroll = 0; */

/*     // Modal */
/*     Modal modal; */
/*     initModal(&modal); */

/*     int executingIndex = -1; */

/*     while (!WindowShouldClose()) { */
/*         Vector2 mousePoint = GetMousePosition(); */

/*         // Update container size */
/*         scrollList.container.width = GetScreenWidth() - 40; */
/*         scrollList.container.height = GetScreenHeight() - 130; */

/*         // Calculate max scroll based on content */
/*         int contentHeight = fileCount * 40; */
/*         scrollList.maxScroll = contentHeight - scrollList.container.height; */
/*         if (scrollList.maxScroll < 0) scrollList.maxScroll = 0; */

/*         // Handle modal input */
/*         if (modal.isOpen) { */
/*             modal.framesCounter++; */

/*             // Mouse click to switch fields */
/*             if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) { */
/*                 if (CheckCollisionPointRec(mousePoint, modal.filenameBox)) { */
/*                     modal.filenameActive = true; */
/*                     modal.commandActive = false; */
/*                 } else if (CheckCollisionPointRec(mousePoint, modal.commandBox)) { */
/*                     modal.filenameActive = false; */
/*                     modal.commandActive = true; */
/*                 } */
/*             } */

/*             // Handle text input for filename */
/*             if (modal.filenameActive) { */
/*                 int key = GetCharPressed(); */
/*                 while (key > 0) { */
/*                     if ((key >= 32) && (key <= 125) && (modal.filenameLength < MAX_FILENAME_CHARS)) { */
/*                         modal.filename[modal.filenameLength] = (char)key; */
/*                         modal.filenameLength++; */
/*                         modal.filename[modal.filenameLength] = '\0'; */
/*                     } */
/*                     key = GetCharPressed(); */
/*                 } */

/*                 if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) { */
/*                     if (modal.filenameLength > 0) { */
/*                         modal.filenameLength--; */
/*                         modal.filename[modal.filenameLength] = '\0'; */
/*                     } */
/*                 } */
/*             } */

/*             // Handle text input for command */
/*             if (modal.commandActive) { */
/*                 int key = GetCharPressed(); */
/*                 while (key > 0) { */
/*                     if ((key >= 32) && (key <= 125) && (modal.commandLength < MAX_COMMAND_CHARS)) { */
/*                         modal.command[modal.commandLength] = (char)key; */
/*                         modal.commandLength++; */
/*                         modal.command[modal.commandLength] = '\0'; */
/*                     } */
/*                     key = GetCharPressed(); */
/*                 } */

/*                 if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) { */
/*                     if (modal.commandLength > 0) { */
/*                         modal.commandLength--; */
/*                         modal.command[modal.commandLength] = '\0'; */
/*                     } */
/*                 } */
/*             } */

/*             // Tab to switch between fields */
/*             if (IsKeyPressed(KEY_TAB)) { */
/*                 modal.filenameActive = !modal.filenameActive; */
/*                 modal.commandActive = !modal.commandActive; */
/*             } */
/*         } else { */
/*             // Normal operation when modal is closed */

/*             // Handle scrolling */
/*             if (CheckCollisionPointRec(mousePoint, scrollList.container)) { */
/*                 float wheel = GetMouseWheelMove(); */
/*                 scrollList.scrollOffset -= wheel * 20; */
/*                 if (scrollList.scrollOffset < 0) scrollList.scrollOffset = 0; */
/*                 if (scrollList.scrollOffset > scrollList.maxScroll) scrollList.scrollOffset = scrollList.maxScroll; */
/*             } */

/*             // Check for "+" button click */
/*             if (CheckCollisionPointRec(mousePoint, addButton)) { */
/*                 if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) { */
/*                     openModal(&modal); */
/*                 } */
/*             } */

/*             // Update file bounds and check for clicks */
/*             float y = scrollList.container.y + 10 - scrollList.scrollOffset; */
/*             float x = scrollList.container.x + 10; */

/*             for (int i = 0; i < fileCount; i++) { */
/*                 // Icon and text bounds */
/*                 files[i].bounds = (Rectangle){ x + 30, y, (float)MeasureText(files[i].displayName, fontSize), (float)fontSize }; */

/*                 // Edit button */
/*                 files[i].editBounds = (Rectangle){  */
/*                     scrollList.container.x + scrollList.container.width - 80,  */
/*                     y - 2, 30, 24  */
/*                 }; */

/*                 // Delete button */
/*                 files[i].deleteBounds = (Rectangle){  */
/*                     scrollList.container.x + scrollList.container.width - 40,  */
/*                     y - 2, 30, 24  */
/*                 }; */

/*                 // Only handle clicks if within container */
/*                 if (y >= scrollList.container.y && y <= scrollList.container.y + scrollList.container.height) { */
/*                     // Check for file name click (execute) */
/*                     if (CheckCollisionPointRec(mousePoint, files[i].bounds)) { */
/*                         if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) { */
/*                             executeFileContent(files[i].filePath); */
/*                             files[i].isExecuting = true; */
/*                             executingIndex = i; */
/*                         } */
/*                     } */

/*                     // Check for edit button click */
/*                     if (CheckCollisionPointRec(mousePoint, files[i].editBounds)) { */
/*                         if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) { */
/*                             openEditModal(&modal, &files[i], i); */
/*                         } */
/*                     } */

/*                     // Check for delete button click */
/*                     if (CheckCollisionPointRec(mousePoint, files[i].deleteBounds)) { */
/*                         if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) { */
/*                             if (deleteScript(files[i].filePath)) { */
/*                                 fileCount = loadFiles(files, scriptDir); */
/*                             } */
/*                         } */
/*                     } */
/*                 } */

/*                 y += 40; */
/*             } */

/*             // Reset executing state */
/*             static int executingTimer = 0; */
/*             if (executingIndex >= 0) { */
/*                 executingTimer++; */
/*                 if (executingTimer > 30) { */
/*                     if (executingIndex < fileCount) { */
/*                         files[executingIndex].isExecuting = false; */
/*                     } */
/*                     executingIndex = -1; */
/*                     executingTimer = 0; */
/*                 } */
/*             } */
/*         } */

/*         // Draw */
/*         BeginDrawing(); */
/*         ClearBackground((Color){45, 45, 48, 255});  // Softer dark background */

/*         if (!modal.isOpen) { */
/*             // Draw normal UI */

/*             // Draw "+" button with rounded corners effect */
/*             Color buttonColor = CheckCollisionPointRec(mousePoint, addButton) ? (Color){80, 80, 85, 255} : (Color){60, 60, 65, 255}; */
/*             DrawRectangleRec(addButton, buttonColor); */
/*             DrawRectangleLinesEx(addButton, 2, (Color){100, 100, 105, 255}); */
/*             DrawTextEx(font, "+", (Vector2){addButton.x + 12, addButton.y + 6}, 32, 2, (Color){220, 220, 220, 255}); */

/*             DrawTextEx(font, "Add Script", (Vector2){60, 22}, 18, 1, (Color){200, 200, 200, 255}); */

/*             // Draw scrollable container with softer style */
/*             DrawRectangleRec(scrollList.container, (Color){35, 35, 38, 255}); */
/*             DrawRectangleLinesEx(scrollList.container, 2, (Color){80, 80, 85, 255}); */

/*             // Enable scissor mode for clipping */
/*             BeginScissorMode( */
/*                 (int)scrollList.container.x,  */
/*                 (int)scrollList.container.y,  */
/*                 (int)scrollList.container.width,  */
/*                 (int)scrollList.container.height */
/*             ); */

/*             // Draw files */
/*             float y = scrollList.container.y + 10 - scrollList.scrollOffset; */
/*             float x = scrollList.container.x + 10; */

/*             for (int i = 0; i < fileCount; i++) { */
/*                 if (y >= scrollList.container.y - 40 && y <= scrollList.container.y + scrollList.container.height) { */
/*                     Color textColor = (Color){220, 220, 220, 255}; */
/*                     Color iconColor = (Color){180, 180, 180, 255}; */

/*                     if (files[i].isExecuting) { */
/*                         textColor = (Color){100, 220, 100, 255}; */
/*                         iconColor = (Color){100, 220, 100, 255}; */
/*                     } */

/*                     // Hover effect */
/*                     if (CheckCollisionPointRec(mousePoint, files[i].bounds)) { */
/*                         DrawRectangle((int)x, (int)(y - 5), (int)scrollList.container.width - 20, 30, (Color){55, 55, 58, 255}); */
/*                     } */

/*                     // Draw file icon */
/*                     drawFileIcon((int)x, (int)y, iconColor); */

/*                     // Draw file name with better font rendering */
/*                     DrawTextEx(font, files[i].displayName, (Vector2){x + 30, y}, fontSize, 1, textColor); */

/*                     // Draw edit button */
/*                     Color editColor = CheckCollisionPointRec(mousePoint, files[i].editBounds) ?  */
/*                                      (Color){255, 200, 100, 255} : (Color){180, 140, 80, 255}; */
/*                     DrawRectangleRec(files[i].editBounds, editColor); */
/*                     DrawRectangleLinesEx(files[i].editBounds, 1, (Color){140, 100, 60, 255}); */
/*                     DrawTextEx(font, "E", (Vector2){files[i].editBounds.x + 10, files[i].editBounds.y + 4}, 16, 1, BLACK); */

/*                     // Draw delete button */
/*                     Color deleteColor = CheckCollisionPointRec(mousePoint, files[i].deleteBounds) ?  */
/*                                        (Color){220, 80, 80, 255} : (Color){180, 60, 60, 255}; */
/*                     DrawRectangleRec(files[i].deleteBounds, deleteColor); */
/*                     DrawRectangleLinesEx(files[i].deleteBounds, 1, (Color){140, 40, 40, 255}); */
/*                     DrawTextEx(font, "X", (Vector2){files[i].deleteBounds.x + 10, files[i].deleteBounds.y + 4}, 16, 1, WHITE); */
/*                 } */

/*                 y += 40; */
/*             } */

/*             EndScissorMode(); */

/*             // Draw scrollbar */
/*             if (scrollList.maxScroll > 0) { */
/*                 float scrollbarHeight = (scrollList.container.height / contentHeight) * scrollList.container.height; */
/*                 float scrollbarY = scrollList.container.y + (scrollList.scrollOffset / scrollList.maxScroll) * (scrollList.container.height - scrollbarHeight); */

/*                 DrawRectangle( */
/*                     (int)(scrollList.container.x + scrollList.container.width - 8),  */
/*                     (int)scrollbarY,  */
/*                     6,  */
/*                     (int)scrollbarHeight,  */
/*                     (Color){120, 120, 125, 255} */
/*                 ); */
/*             } */

/*             // Draw info bar */
/*             #ifdef PLATFORM_WINDOWS */
/*                 DrawTextEx(font, TextFormat("Windows | Scripts: %d | Format: .bat", fileCount),  */
/*                         (Vector2){10, GetScreenHeight() - 30}, 18, 1, (Color){180, 180, 180, 255}); */
/*             #else */
/*                 DrawTextEx(font, TextFormat("Linux | Scripts: %d | Format: .sh", fileCount),  */
/*                         (Vector2){10, GetScreenHeight() - 30}, 18, 1, (Color){180, 180, 180, 255}); */
/*             #endif */
/*         } else { */
/*             // Draw modal */

/*             // Semi-transparent overlay */
/*             DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), (Color){0, 0, 0, 180}); */

/*             // Modal box */
/*             int modalWidth = 500; */
/*             int modalHeight = 300; */
/*             int modalX = (GetScreenWidth() - modalWidth) / 2; */
/*             int modalY = (GetScreenHeight() - modalHeight) / 2; */

/*             Rectangle modalBox = { (float)modalX, (float)modalY, (float)modalWidth, (float)modalHeight }; */
/*             DrawRectangleRec(modalBox, (Color){240, 240, 245, 255}); */
/*             DrawRectangleLinesEx(modalBox, 3, (Color){60, 60, 65, 255}); */

/*             // Title */
/*             const char *title = modal.isEditMode ? "Edit Script" : "Create New Script"; */
/*             DrawTextEx(font, title, (Vector2){modalX + 20, modalY + 20}, 24, 1, (Color){40, 40, 45, 255}); */

/*             // Filename input */
/*             DrawTextEx(font, "Filename:", (Vector2){modalX + 20, modalY + 60}, 18, 1, (Color){60, 60, 65, 255}); */
/*             modal.filenameBox = (Rectangle){ (float)(modalX + 20), (float)(modalY + 85), (float)(modalWidth - 40), 35 }; */
/*             DrawRectangleRec(modal.filenameBox, WHITE); */
/*             DrawRectangleLinesEx(modal.filenameBox, 2, modal.filenameActive ? (Color){80, 150, 255, 255} : (Color){140, 140, 145, 255}); */
/*             DrawTextEx(font, modal.filename, (Vector2){modalX + 25, modalY + 93}, 20, 1, BLACK); */

/*             // Draw cursor for filename */
/*             if (modal.filenameActive && ((modal.framesCounter / 20) % 2) == 0) { */
/*                 DrawTextEx(font, "_", (Vector2){modalX + 25 + MeasureText(modal.filename, 20), modalY + 93}, 20, 1, BLACK); */
/*             } */

/*             // Command input */
/*             DrawTextEx(font, "Command:", (Vector2){modalX + 20, modalY + 135}, 18, 1, (Color){60, 60, 65, 255}); */
/*             modal.commandBox = (Rectangle){ (float)(modalX + 20), (float)(modalY + 160), (float)(modalWidth - 40), 60 }; */
/*             DrawRectangleRec(modal.commandBox, WHITE); */
/*             DrawRectangleLinesEx(modal.commandBox, 2, modal.commandActive ? (Color){80, 150, 255, 255} : (Color){140, 140, 145, 255}); */
/*             DrawTextEx(font, modal.command, (Vector2){modalX + 25, modalY + 168}, 18, 1, BLACK); */

/*             // Draw cursor for command */
/*             if (modal.commandActive && ((modal.framesCounter / 20) % 2) == 0) { */
/*                 DrawTextEx(font, "_", (Vector2){modalX + 25 + MeasureText(modal.command, 18), modalY + 168}, 18, 1, BLACK); */
/*             } */

/*             // Buttons */
/*             Rectangle saveButton = { (float)(modalX + modalWidth - 220), (float)(modalY + modalHeight - 50), 90, 35 }; */
/*             Rectangle cancelButton = { (float)(modalX + modalWidth - 120), (float)(modalY + modalHeight - 50), 90, 35 }; */

/*             // Save button */
/*             Color saveColor = CheckCollisionPointRec(mousePoint, saveButton) ?  */
/*                              (Color){80, 200, 120, 255} : (Color){60, 180, 100, 255}; */
/*             DrawRectangleRec(saveButton, saveColor); */
/*             DrawRectangleLinesEx(saveButton, 2, (Color){40, 140, 80, 255}); */
/*             DrawTextEx(font, "Save", (Vector2){saveButton.x + 25, saveButton.y + 8}, 20, 1, WHITE); */

/*             if (CheckCollisionPointRec(mousePoint, saveButton) && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) { */
/*                 if (modal.isEditMode && modal.editIndex >= 0 && modal.editIndex < fileCount) { */
/*                     deleteScript(files[modal.editIndex].filePath); */
/*                 } */

/*                 if (saveNewScript(scriptDir, modal.filename, modal.command)) { */
/*                     fileCount = loadFiles(files, scriptDir); */
/*                     closeModal(&modal); */
/*                 } */
/*             } */

/*             // Cancel button */
/* 	    Color cancelColor = CheckCollisionPointRec(mousePoint, cancelButton) ?  */
/*                    (Color){220, 80, 80, 255} : (Color){200, 60, 60, 255}; */
/*             DrawRectangleRec(cancelButton, cancelColor); */
/*             DrawRectangleLinesEx(cancelButton, 2, (Color){160, 40, 40, 255}); */
/*             DrawTextEx(font, "Cancel", (Vector2){cancelButton.x + 15, cancelButton.y + 8}, 20, 1, WHITE); */

/*             if (CheckCollisionPointRec(mousePoint, cancelButton) && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) { */
/*                 closeModal(&modal); */
/*             } */

/*             // Helper text */
/*             DrawTextEx(font, "Click fields or press TAB to switch", (Vector2){modalX + 20, modalY + modalHeight - 50}, 14, 1, (Color){100, 100, 105, 255}); */
/*         } */

/*         EndDrawing();  */
/*     } */

/*     CloseWindow(); */
/*     return 0; */
/* } */


/* #include "raylib.h" */
/* #include <stdio.h> */
/* #include <string.h> // Required for string handling */
/* #include <dirent.h> */
/* #include <string.h> */

/* #define MAX_INPUT_CHARS 100 */
/* #define screenWidth 800 */
/* #define screenHeight 500 */
/* #define intialFPS 60 */
/* #define MAX_FILES 256 */
/* #define fontSize 20 */

/* DIR * getAllScriptNamesFromSpecifiedDirectory (char *dir) */
/* { */

/*   DIR *folder; */
/*   folder = opendir(dir); */
/*   if(folder == NULL) */
/*     { */
/*       perror("Unable to read directory"); */
/*       closedir(folder); */
/*       return NULL;   */
/*     } */

/*   return folder;  */
/* } */


/* int main() { */


/*   char *filenames[MAX_FILES]; */
/*   int files = 0; */

/*   DIR *dir = opendir("./scripts"); */
/*   struct dirent *entry; */
/*   while ((entry = readdir(dir)) && files < MAX_FILES) { */
/*     if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue; */
/*     char fileName[256];   */
/*     size_t len = strlen(entry->d_name); */
/*     size_t dotIndex = len;   */
/*     for (size_t x = 0; x < len; x++) { */
/*       if (entry->d_name[x] == '.') {  */
/*         dotIndex = x; */
/*         break;  */
/*       } */
/*     } */
/*     strncpy(fileName, entry->d_name, dotIndex); */
/*     fileName[dotIndex] = '\0';  */
/*     filenames[files] = strdup(fileName); */
/*     files++; */
/*   } */
/*   closedir(dir); */


/*   SetConfigFlags(FLAG_WINDOW_RESIZABLE); */

/*   InitWindow(screenWidth, screenHeight, "k0rT"); */

/*   SetTargetFPS(intialFPS); */

/*   while (!WindowShouldClose()) { */
/*     BeginDrawing(); */
/*     ClearBackground(DARKGRAY); */

/*     /\* DrawRectangleLines(20, GetScreenHeight()/2, GetScreenWidth()-40, GetScreenHeight()/2-40 , WHITE); *\/ */
/*     int y = 50; */
/*     int x = 50; */
/*     for (int i = 0; i < files; i++) { */
/*       int textWidth = MeasureText(filenames[i], fontSize); */
/*       Rectangle textBox = { (float)x, (float)y, (float)textWidth, (float)fontSize }; */
/*       bool clicked = false; */
/*       DrawText(filenames[i], x, y, fontSize, BLACK); */
/*       y += 30; */
/*     } */

/*     EndDrawing();  */
/*     /\* int frameRateInInteger = GetFPS(); *\/ */
/*     /\* int key = GetCharPressed(); *\/ */
/*     /\* while (key > 0) { *\/ */
/*     /\*   if ((key >= 32) && (key <= 125) && (letterCount < MAX_INPUT_CHARS)) { *\/ */
/*     /\*     name[letterCount] = (char)key; *\/ */
/*     /\*     letterCount++; *\/ */
/*     /\*     name[letterCount] = '\0'; *\/ */
/*     /\*   } *\/ */

/*     /\*   key = GetCharPressed(); *\/ */
/*     /\* } *\/ */

/*     /\* if (IsKeyPressedRepeat(KEY_BACKSPACE) || IsKeyPressed(KEY_BACKSPACE)) { *\/ */
/*     /\*   if (letterCount > 0) { *\/ */
/*     /\*     letterCount--; *\/ */
/*     /\*     name[letterCount] = '\0'; *\/ */
/*     /\*   } *\/ */
/*     /\* } *\/ */
/*     /\* framesCounter++; *\/ */
/*     /\* // Draw *\/ */
/*     /\* BeginDrawing(); *\/ */

/*     /\* ClearBackground(RAYWHITE); *\/ */
/*     /\* DrawText("TYPE YOUR NAME:", *\/ */
/*     /\*          (GetScreenWidth() / 2) - MeasureText("TYPE YOUR NAME:", 20) / 2, 0, 20, *\/ */
/*     /\*          GRAY); *\/ */
/*     /\* DrawText(TextFormat("%d",frameRateInInteger), 240 + 5, 180 + 8, 40, BLACK); *\/ */
/*     /\* DrawText(name, 240 + 5, 180 + 8, 40, MAROON); *\/ */

/*     /\* if (((framesCounter / 20) % 2) == 0) { *\/ */
/*     /\*   DrawText("_", 240 + 8 + MeasureText(name, 40), 180 + 12, 40, MAROON); *\/ */
/*     /\* } *\/ */

/*     /\* EndDrawing(); *\/ */
/*   } */

/*   CloseWindow(); */

/*   return 0; */
/* } */
