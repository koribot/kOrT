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
    #undef NOGDI
    #undef NOUSER

    // Declare only what we need from shell32
    #ifdef __cplusplus
    extern "C" {
    #endif
    __declspec(dllimport) void* __stdcall ShellExecuteA(void*, const char*, const char*, const char*, const char*, int);
    #ifdef __cplusplus
    }
    #endif
    #define SW_SHOW 5
#else
    #define PLATFORM_LINUX
    #include <unistd.h>
    #include <sys/stat.h>
    #include <sys/types.h>
#endif

#include "raylib.h"

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
    // Text editor cursor
    int cursorPos;
    int selectionStart;
    int selectionEnd;
    bool hasSelection;
    // Scroll control
    bool isManualScrolling;
    int manualScrollTimer;
    bool isDraggingScrollbar;
    float scrollbarDragOffset;
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
    char *content = readFileContent(filepath);
    if (content == NULL) {
        return;
    }

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
        fclose(temp);

        // Use ShellExecuteA instead of system() for better performance
        char cmdArgs[4096];
        snprintf(cmdArgs, sizeof(cmdArgs), "/c \"%s\"", tempFile);
        ShellExecuteA(NULL, "open", "cmd.exe", cmdArgs, NULL, SW_SHOW);
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
    // Use ShellExecuteA instead of system() for better performance
    ShellExecuteA(NULL, "open", scriptDir, NULL, NULL, SW_SHOW);
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

    return true;
}

// Delete script file
bool deleteScript(const char *filepath) {
    if (remove(filepath) == 0) {
        return true;
    } else {
        return false;
    }
}

// Reload files from directory
int loadFiles(FileItem *files, const char *scriptDir) {
    int fileCount = 0;

    DIR *dir = opendir(scriptDir);
    if (dir == NULL) {
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
        modal->cursorPos = modal->commandLength;
        modal->hasSelection = false;
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
        modal->cursorPos = modal->commandLength;
        modal->hasSelection = false;

        // Remove the auto-pushed undo (we just did that)
        modal->undoStackSize--;
    }
}

// Insert text at cursor position
void insertTextAtCursor(Modal *modal, const char *text) {
    int textLen = strlen(text);

    // Delete selection if exists
    if (modal->hasSelection) {
        int selStart = modal->selectionStart < modal->selectionEnd ? modal->selectionStart : modal->selectionEnd;
        int selEnd = modal->selectionStart > modal->selectionEnd ? modal->selectionStart : modal->selectionEnd;

        memmove(modal->command + selStart, modal->command + selEnd, modal->commandLength - selEnd + 1);
        modal->commandLength -= (selEnd - selStart);
        modal->cursorPos = selStart;
        modal->hasSelection = false;
    }

    if (modal->commandLength + textLen < MAX_COMMAND_CHARS) {
        // Shift text to make room
        memmove(modal->command + modal->cursorPos + textLen,
                modal->command + modal->cursorPos,
                modal->commandLength - modal->cursorPos + 1);

        // Insert new text
        memcpy(modal->command + modal->cursorPos, text, textLen);
        modal->commandLength += textLen;
        modal->cursorPos += textLen;
    }
}

// Delete character at cursor
void deleteCharAtCursor(Modal *modal, bool isBackspace) {
    if (modal->hasSelection) {
        int selStart = modal->selectionStart < modal->selectionEnd ? modal->selectionStart : modal->selectionEnd;
        int selEnd = modal->selectionStart > modal->selectionEnd ? modal->selectionStart : modal->selectionEnd;

        memmove(modal->command + selStart, modal->command + selEnd, modal->commandLength - selEnd + 1);
        modal->commandLength -= (selEnd - selStart);
        modal->cursorPos = selStart;
        modal->hasSelection = false;
    } else if (isBackspace && modal->cursorPos > 0) {
        memmove(modal->command + modal->cursorPos - 1,
                modal->command + modal->cursorPos,
                modal->commandLength - modal->cursorPos + 1);
        modal->cursorPos--;
        modal->commandLength--;
    } else if (!isBackspace && modal->cursorPos < modal->commandLength) {
        memmove(modal->command + modal->cursorPos,
                modal->command + modal->cursorPos + 1,
                modal->commandLength - modal->cursorPos);
        modal->commandLength--;
    }
}

// Get cursor position (line, column) from cursor index
void getCursorLineCol(const char *text, int cursorPos, int *line, int *col) {
    *line = 0;
    *col = 0;

    for (int i = 0; i < cursorPos && i < strlen(text); i++) {
        if (text[i] == '\n') {
            (*line)++;
            *col = 0;
        } else {
            (*col)++;
        }
    }
}

// Get cursor index from line and column
int getCursorPosFromLineCol(const char *text, int targetLine, int targetCol) {
    int line = 0;
    int col = 0;

    for (int i = 0; i <= strlen(text); i++) {
        if (line == targetLine && col == targetCol) {
            return i;
        }

        if (text[i] == '\n') {
            if (line == targetLine) {
                return i; // End of line
            }
            line++;
            col = 0;
        } else if (text[i] == '\0') {
            return i;
        } else {
            col++;
        }
    }

    return strlen(text);
}

// Get cursor position from mouse click
int getCursorPosFromMouse(Font font, const char *text, int mouseX, int mouseY, Rectangle box, float scrollY) {
    int lineHeight = fontSize + 4;
    int startX = (int)box.x + 45;
    int startY = (int)box.y;

    // Account for scroll offset when calculating which line was clicked
    int adjustedMouseY = mouseY + (int)scrollY;
    int clickedLine = (adjustedMouseY - startY) / lineHeight;
    if (clickedLine < 0) clickedLine = 0;

    // Count lines in text
    int totalLines = 1;
    for (int i = 0; i < strlen(text); i++) {
        if (text[i] == '\n') totalLines++;
    }
    if (clickedLine >= totalLines) clickedLine = totalLines - 1;

    // Find start of clicked line
    int currentLine = 0;
    int lineStart = 0;
    for (int i = 0; i <= strlen(text); i++) {
        if (currentLine == clickedLine) {
            lineStart = i;
            break;
        }
        if (text[i] == '\n') {
            currentLine++;
        }
    }

    // Find end of clicked line
    int lineEnd = lineStart;
    while (lineEnd < strlen(text) && text[lineEnd] != '\n') {
        lineEnd++;
    }

    // Extract the line
    char line[MAX_COMMAND_CHARS];
    int lineLen = lineEnd - lineStart;
    strncpy(line, text + lineStart, lineLen);
    line[lineLen] = '\0';

    // Find closest character in the line
    int relativeX = mouseX - startX;
    int bestPos = lineStart;
    int minDist = 10000;

    for (int i = 0; i <= lineLen; i++) {
        char substr[MAX_COMMAND_CHARS];
        strncpy(substr, line, i);
        substr[i] = '\0';

        Vector2 size = MeasureTextEx(font, substr, (float)fontSize, 1.0f);
        int dist = abs((int)size.x - relativeX);

        if (dist < minDist) {
            minDist = dist;
            bestPos = lineStart + i;
        }
    }

    return bestPos;
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
    modal->cursorPos = 0;
    modal->selectionStart = 0;
    modal->selectionEnd = 0;
    modal->hasSelection = false;
    modal->isManualScrolling = false;
    modal->manualScrollTimer = 0;
    modal->isDraggingScrollbar = false;
    modal->scrollbarDragOffset = 0;
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
    modal->cursorPos = 0;
    modal->selectionStart = 0;
    modal->selectionEnd = 0;
    modal->hasSelection = false;
    modal->isManualScrolling = false;
    modal->manualScrollTimer = 0;
    modal->isDraggingScrollbar = false;
    modal->scrollbarDragOffset = 0;
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
    modal->cursorPos = modal->commandLength;
    modal->selectionStart = 0;
    modal->selectionEnd = 0;
    modal->hasSelection = false;
    modal->isManualScrolling = false;
    modal->manualScrollTimer = 0;
    modal->isDraggingScrollbar = false;
    modal->scrollbarDragOffset = 0;

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

// Draw command text with line numbers and scrolling - OPTIMIZED VERSION
void DrawCommandWithLineNumbers(Font font, bool useFont,
                                const char *text,
                                Rectangle box,
                                float scrollY,
                                int _fontSize,
                                int cursorPos,
                                bool showCursor,
                                int selStart,
                                int selEnd,
                                bool hasSelection) {

    // Draw line number background OUTSIDE scissor mode
    DrawRectangle((int)box.x, (int)box.y, 40, (int)box.height, (Color){50, 52, 64, 255});
    DrawLine((int)box.x + 40, (int)box.y, (int)box.x + 40, (int)box.y + (int)box.height, (Color){98, 114, 164, 255});

    // Start scissor mode for scrollable content
    BeginScissorMode(
        (int)box.x,
        (int)box.y,
        (int)box.width,
        (int)box.height
    );

    int lineHeight = _fontSize + 4;
    float yOffset = -scrollY;
    int lineNumber = 1;

    // Normalize selection
    int actualSelStart = selStart;
    int actualSelEnd = selEnd;
    if (hasSelection && selStart > selEnd) {
        actualSelStart = selEnd;
        actualSelEnd = selStart;
    }

    int charIndex = 0;
    int textLen = strlen(text);
    int lineStart = 0;

    // Calculate visible range to skip rendering invisible lines
    int firstVisibleLine = (int)(scrollY / lineHeight);
    if (firstVisibleLine < 0) firstVisibleLine = 0;

    int visibleLines = (int)(box.height / lineHeight) + 2; // +2 for partial lines

    // Parse lines without modifying original text
    for (int i = 0; i <= textLen; i++) {
        bool isNewline = (text[i] == '\n' || text[i] == '\0');

        if (isNewline) {
            float lineY = box.y + yOffset;

            // Only process visible lines
            if (lineNumber >= firstVisibleLine && lineNumber < firstVisibleLine + visibleLines) {
                // Draw line number
                DrawText(TextFormat("%d", lineNumber),
                         (int)(box.x + 5),
                         (int)lineY,
                         _fontSize,
                         (Color){139, 233, 253, 255});

                int lineLen = i - lineStart;

                // Draw selection highlight for this line
                if (hasSelection && lineLen > 0) {
                    int lineCharStart = charIndex;
                    int lineCharEnd = charIndex + lineLen;

                    if (actualSelEnd > lineCharStart && actualSelStart < lineCharEnd) {
                        int selStartInLine = actualSelStart > lineCharStart ? actualSelStart - lineCharStart : 0;
                        int selEndInLine = actualSelEnd < lineCharEnd ? actualSelEnd - lineCharStart : lineLen;

                        // Measure text before selection
                        char beforeSel[512];
                        int beforeLen = selStartInLine > 511 ? 511 : selStartInLine;
                        strncpy(beforeSel, text + lineStart, beforeLen);
                        beforeSel[beforeLen] = '\0';

                        // Measure selected text
                        char selected[512];
                        int selLen = (selEndInLine - selStartInLine) > 511 ? 511 : (selEndInLine - selStartInLine);
                        strncpy(selected, text + lineStart + selStartInLine, selLen);
                        selected[selLen] = '\0';

                        Vector2 beforeSize = MeasureTextEx(font, beforeSel, (float)_fontSize, 1.0f);
                        Vector2 selSize = MeasureTextEx(font, selected, (float)_fontSize, 1.0f);

                        DrawRectangle((int)(box.x + 45 + beforeSize.x), (int)lineY,
                                    (int)selSize.x, lineHeight, (Color){80, 120, 200, 180});
                    }
                }

                // Draw line text (only if not empty)
                if (lineLen > 0) {
                    char lineText[512];
                    int copyLen = lineLen > 511 ? 511 : lineLen;
                    strncpy(lineText, text + lineStart, copyLen);
                    lineText[copyLen] = '\0';

                    DrawTextEx(font,
                               lineText,
                               (Vector2){box.x + 45, lineY},
                               (float)_fontSize,
                               1.0f,
                               (Color){248, 248, 242, 255});
                }

                // Draw cursor if it's on this line
                if (showCursor && cursorPos >= charIndex && cursorPos <= charIndex + lineLen) {
                    int cursorPosInLine = cursorPos - charIndex;

                    char beforeCursor[512];
                    int beforeLen = cursorPosInLine > 511 ? 511 : cursorPosInLine;
                    strncpy(beforeCursor, text + lineStart, beforeLen);
                    beforeCursor[beforeLen] = '\0';

                    Vector2 beforeSize = MeasureTextEx(font, beforeCursor, (float)_fontSize, 1.0f);

                    // Draw cursor line
                    DrawRectangle((int)(box.x + 45 + beforeSize.x), (int)lineY,
                                2, lineHeight, (Color){248, 248, 242, 255});
                }
            }

            charIndex += (i - lineStart) + 1; // +1 for the newline
            yOffset += lineHeight;
            lineNumber++;
            lineStart = i + 1;

            if (text[i] == '\0') break;
        }
    }

    EndScissorMode();
}

int main() {
    FileItem files[MAX_FILES];
    int fileCount = 0;
    char scriptDir[512];
    getScriptsPath(scriptDir, sizeof(scriptDir));

    fileCount = loadFiles(files, scriptDir);

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
        }
    #else
        if (FileExists("fonts/ttf/JetBrainsMono-Light.ttf")) {
            customFont = LoadFont("fonts/ttf/JetBrainsMono-Light.ttf");
            useCustomFont = true;
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

    bool isMouseDragging = false;

    while (!WindowShouldClose()) {
        Vector2 mousePoint = GetMousePosition();

        scrollList.container.width = GetScreenWidth() - 40;
        scrollList.container.height = GetScreenHeight() - 130;

        int contentHeight = fileCount * 40;
        scrollList.maxScroll = contentHeight - scrollList.container.height;
        if (scrollList.maxScroll < 0) scrollList.maxScroll = 0;

        if (modal.isOpen) {
            modal.framesCounter++;

            // Decrease manual scroll timer (if not set to -1 for permanent manual mode)
            if (modal.manualScrollTimer > 0) {
                modal.manualScrollTimer--;
                if (modal.manualScrollTimer == 0) {
                    modal.isManualScrolling = false;
                    printf("[SCROLL] Manual scroll timer expired, auto-scroll re-enabled\n");
                }
            }
            // If timer is -1, manual scroll mode is permanent until keyboard navigation

            // Calculate scrollbar dimensions for dragging
            int modalWidth = 800;
            int modalHeight = 550;
            int modalX = (GetScreenWidth() - modalWidth) / 2;
            int modalY = (GetScreenHeight() - modalHeight) / 2;

            // Count lines for scrollbar calculation
            int lineCount = 1;
            for (int i = 0; i < modal.commandLength; i++) {
                if (modal.command[i] == '\n') lineCount++;
            }
            int lineHeight = fontSize + 4;
            float contentHeight = lineCount * lineHeight;
            modal.commandMaxScrollY = contentHeight - 290;
            if (modal.commandMaxScrollY < 0) modal.commandMaxScrollY = 0;

            Rectangle scrollbarRect = {0};
            if (modal.commandMaxScrollY > 0) {
                float scrollbarHeight = (290 / contentHeight) * 290;
                if (scrollbarHeight < 20) scrollbarHeight = 20;
                float scrollbarY = modalY + 160 + (modal.commandScrollOffsetY / modal.commandMaxScrollY) * (290 - scrollbarHeight);
                scrollbarRect = (Rectangle){modalX + modalWidth - 30, scrollbarY, 6, scrollbarHeight};
            }

            // Handle scrollbar dragging
            if (modal.commandMaxScrollY > 0) {
                if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && CheckCollisionPointRec(mousePoint, scrollbarRect)) {
                    modal.isDraggingScrollbar = true;
                    modal.scrollbarDragOffset = mousePoint.y - scrollbarRect.y;
                    modal.isManualScrolling = true;
                    modal.manualScrollTimer = -1; // Set to -1 to disable auto-scroll indefinitely
                    printf("[SCROLL] Scrollbar drag started at offset %.2f - auto-scroll disabled permanently\n", modal.scrollbarDragOffset);
                }

                if (IsMouseButtonDown(MOUSE_LEFT_BUTTON) && modal.isDraggingScrollbar) {
                    // Calculate new scroll position based on mouse Y
                    float scrollbarTrackHeight = 290 - scrollbarRect.height;
                    float newScrollbarY = mousePoint.y - modal.scrollbarDragOffset;
                    float scrollbarTopLimit = modalY + 160;
                    float scrollbarBottomLimit = modalY + 160 + scrollbarTrackHeight;

                    // Clamp scrollbar position
                    if (newScrollbarY < scrollbarTopLimit) newScrollbarY = scrollbarTopLimit;
                    if (newScrollbarY > scrollbarBottomLimit) newScrollbarY = scrollbarBottomLimit;

                    // Convert scrollbar position to scroll offset
                    float scrollbarRatio = (newScrollbarY - scrollbarTopLimit) / scrollbarTrackHeight;
                    modal.commandScrollOffsetY = scrollbarRatio * modal.commandMaxScrollY;

                    printf("[SCROLL] Dragging scrollbar - ratio: %.2f, offset: %.2f\n", scrollbarRatio, modal.commandScrollOffsetY);
                }
            }

            // Handle vertical scrolling in command box only (mouse wheel)
            if (CheckCollisionPointRec(mousePoint, modal.commandBox) && !modal.isDraggingScrollbar) {
                float wheel = GetMouseWheelMove();
                if (wheel != 0) {
                    // Mark as manual scrolling and disable auto-scroll permanently
                    modal.isManualScrolling = true;
                    modal.manualScrollTimer = -1; // Set to -1 to disable timer

                    modal.commandScrollOffsetY -= wheel * 30;

                    // Clamp immediately
                    if (modal.commandScrollOffsetY < 0) modal.commandScrollOffsetY = 0;
                    if (modal.commandScrollOffsetY > modal.commandMaxScrollY) {
                        modal.commandScrollOffsetY = modal.commandMaxScrollY;
                    }

                    printf("[SCROLL] Mouse wheel scroll - offset: %.2f, max: %.2f - auto-scroll disabled permanently\n", modal.commandScrollOffsetY, modal.commandMaxScrollY);
                }
            }

            // Mouse click to switch fields and set cursor position
            if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && !modal.isDraggingScrollbar) {
                if (CheckCollisionPointRec(mousePoint, modal.filenameBox)) {
                    modal.filenameActive = true;
                    modal.commandActive = false;
                } else if (CheckCollisionPointRec(mousePoint, modal.commandBox)) {
                    modal.filenameActive = false;
                    modal.commandActive = true;

                    // Set cursor position from mouse click (only if not on scrollbar)
                    if (useCustomFont && !modal.isDraggingScrollbar) {
                        modal.cursorPos = getCursorPosFromMouse(customFont, modal.command,
                                                               (int)mousePoint.x, (int)mousePoint.y,
                                                               modal.commandBox, modal.commandScrollOffsetY);
                        modal.hasSelection = false;
                        isMouseDragging = true;
                        modal.selectionStart = modal.cursorPos;
                    }
                }
            }

            // Handle mouse dragging for selection (but not when dragging scrollbar)
            if (IsMouseButtonDown(MOUSE_LEFT_BUTTON) && isMouseDragging && modal.commandActive && !modal.isDraggingScrollbar) {
                if (useCustomFont) {
                    int newPos = getCursorPosFromMouse(customFont, modal.command,
                                                      (int)mousePoint.x, (int)mousePoint.y,
                                                      modal.commandBox, modal.commandScrollOffsetY);
                    modal.cursorPos = newPos;
                    modal.selectionEnd = newPos;
                    if (newPos != modal.selectionStart) {
                        modal.hasSelection = true;
                    } else {
                        modal.hasSelection = false;
                    }
                }
            }

            if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON)) {
                isMouseDragging = false;
                if (modal.isDraggingScrollbar) {
                    modal.isDraggingScrollbar = false;
                    printf("[SCROLL] Scrollbar drag ended - manual scroll remains active\n");
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
                        if (modal.filenameLength > 0) {
                            SetClipboardText(modal.filename);
                        }
                    }
                }
            }

            // Handle text input for command (multi-line with full editor features)
            if (modal.commandActive) {
                bool shiftPressed = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
                bool ctrlPressed = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);

                // Undo/Redo (with repeat support for holding keys)
                if (ctrlPressed) {
                    if ((IsKeyPressed(KEY_Z) || IsKeyPressedRepeat(KEY_Z)) && !shiftPressed) {
                        performUndo(&modal);
                    } else if (IsKeyPressed(KEY_Y) || IsKeyPressedRepeat(KEY_Y) || (shiftPressed && (IsKeyPressed(KEY_Z) || IsKeyPressedRepeat(KEY_Z)))) {
                        performRedo(&modal);
                    } else if (IsKeyPressed(KEY_A)) {
                        // Select all
                        modal.selectionStart = 0;
                        modal.selectionEnd = modal.commandLength;
                        modal.hasSelection = true;
                        modal.cursorPos = modal.commandLength;
                    } else if (IsKeyPressed(KEY_C)) {
                        // Copy
                        if (modal.hasSelection) {
                            int start = modal.selectionStart < modal.selectionEnd ? modal.selectionStart : modal.selectionEnd;
                            int end = modal.selectionStart > modal.selectionEnd ? modal.selectionStart : modal.selectionEnd;
                            char temp[MAX_COMMAND_CHARS];
                            strncpy(temp, modal.command + start, end - start);
                            temp[end - start] = '\0';
                            SetClipboardText(temp);
                        }
                    } else if (IsKeyPressed(KEY_X)) {
                        // Cut
                        if (modal.hasSelection) {
                            int start = modal.selectionStart < modal.selectionEnd ? modal.selectionStart : modal.selectionEnd;
                            int end = modal.selectionStart > modal.selectionEnd ? modal.selectionStart : modal.selectionEnd;
                            char temp[MAX_COMMAND_CHARS];
                            strncpy(temp, modal.command + start, end - start);
                            temp[end - start] = '\0';
                            SetClipboardText(temp);

                            pushUndo(&modal);
                            deleteCharAtCursor(&modal, true);
                        }
                    } else if (IsKeyPressed(KEY_V)) {
                        // Paste
                        const char *clipText = GetClipboardText();
                        if (clipText != NULL) {
                            pushUndo(&modal);
                            insertTextAtCursor(&modal, clipText);
                        }
                    }
                }

                // Arrow key navigation
                if (IsKeyPressed(KEY_LEFT) || IsKeyPressedRepeat(KEY_LEFT)) {
                    if (shiftPressed) {
                        if (!modal.hasSelection) {
                            modal.selectionStart = modal.cursorPos;
                            modal.hasSelection = true;
                        }
                        if (modal.cursorPos > 0) modal.cursorPos--;
                        modal.selectionEnd = modal.cursorPos;
                    } else {
                        if (modal.hasSelection) {
                            modal.cursorPos = modal.selectionStart < modal.selectionEnd ? modal.selectionStart : modal.selectionEnd;
                            modal.hasSelection = false;
                        } else if (modal.cursorPos > 0) {
                            modal.cursorPos--;
                        }
                    }
                    // Trigger auto-scroll on keyboard navigation
                    modal.isManualScrolling = false;
                    modal.manualScrollTimer = 0;
                    printf("[SCROLL] Cursor moved via keyboard (LEFT), auto-scroll enabled\n");
                }

                if (IsKeyPressed(KEY_RIGHT) || IsKeyPressedRepeat(KEY_RIGHT)) {
                    if (shiftPressed) {
                        if (!modal.hasSelection) {
                            modal.selectionStart = modal.cursorPos;
                            modal.hasSelection = true;
                        }
                        if (modal.cursorPos < modal.commandLength) modal.cursorPos++;
                        modal.selectionEnd = modal.cursorPos;
                    } else {
                        if (modal.hasSelection) {
                            modal.cursorPos = modal.selectionStart > modal.selectionEnd ? modal.selectionStart : modal.selectionEnd;
                            modal.hasSelection = false;
                        } else if (modal.cursorPos < modal.commandLength) {
                            modal.cursorPos++;
                        }
                    }
                    // Trigger auto-scroll on keyboard navigation
                    modal.isManualScrolling = false;
                    modal.manualScrollTimer = 0;
                    printf("[SCROLL] Cursor moved via keyboard (RIGHT), auto-scroll enabled\n");
                }

                if (IsKeyPressed(KEY_UP) || IsKeyPressedRepeat(KEY_UP)) {
                    int line, col;
                    getCursorLineCol(modal.command, modal.cursorPos, &line, &col);
                    if (line > 0) {
                        int newPos = getCursorPosFromLineCol(modal.command, line - 1, col);
                        if (shiftPressed) {
                            if (!modal.hasSelection) {
                                modal.selectionStart = modal.cursorPos;
                                modal.hasSelection = true;
                            }
                            modal.cursorPos = newPos;
                            modal.selectionEnd = modal.cursorPos;
                        } else {
                            modal.cursorPos = newPos;
                            modal.hasSelection = false;
                        }
                        // Trigger auto-scroll on keyboard navigation
                        modal.isManualScrolling = false;
                        modal.manualScrollTimer = 0;
                        printf("[SCROLL] Cursor moved via keyboard (UP), auto-scroll enabled\n");
                    }
                }

                if (IsKeyPressed(KEY_DOWN) || IsKeyPressedRepeat(KEY_DOWN)) {
                    int line, col;
                    getCursorLineCol(modal.command, modal.cursorPos, &line, &col);
                    int newPos = getCursorPosFromLineCol(modal.command, line + 1, col);
                    if (newPos != modal.cursorPos) {
                        if (shiftPressed) {
                            if (!modal.hasSelection) {
                                modal.selectionStart = modal.cursorPos;
                                modal.hasSelection = true;
                            }
                            modal.cursorPos = newPos;
                            modal.selectionEnd = modal.cursorPos;
                        } else {
                            modal.cursorPos = newPos;
                            modal.hasSelection = false;
                        }
                        // Trigger auto-scroll on keyboard navigation
                        modal.isManualScrolling = false;
                        modal.manualScrollTimer = 0;
                        printf("[SCROLL] Cursor moved via keyboard (DOWN), auto-scroll enabled\n");
                    }
                }

                if (IsKeyPressed(KEY_HOME)) {
                    int line, col;
                    getCursorLineCol(modal.command, modal.cursorPos, &line, &col);
                    int newPos = getCursorPosFromLineCol(modal.command, line, 0);
                    if (shiftPressed) {
                        if (!modal.hasSelection) {
                            modal.selectionStart = modal.cursorPos;
                            modal.hasSelection = true;
                        }
                        modal.cursorPos = newPos;
                        modal.selectionEnd = modal.cursorPos;
                    } else {
                        modal.cursorPos = newPos;
                        modal.hasSelection = false;
                    }
                    // Trigger auto-scroll on keyboard navigation
                    modal.isManualScrolling = false;
                    modal.manualScrollTimer = 0;
                    printf("[SCROLL] Cursor moved via keyboard (HOME), auto-scroll enabled\n");
                }

                if (IsKeyPressed(KEY_END)) {
                    int line, col;
                    getCursorLineCol(modal.command, modal.cursorPos, &line, &col);
                    int newPos = getCursorPosFromLineCol(modal.command, line, 10000);
                    if (shiftPressed) {
                        if (!modal.hasSelection) {
                            modal.selectionStart = modal.cursorPos;
                            modal.hasSelection = true;
                        }
                        modal.cursorPos = newPos;
                        modal.selectionEnd = modal.cursorPos;
                    } else {
                        modal.cursorPos = newPos;
                        modal.hasSelection = false;
                    }
                    // Trigger auto-scroll on keyboard navigation
                    modal.isManualScrolling = false;
                    modal.manualScrollTimer = 0;
                    printf("[SCROLL] Cursor moved via keyboard (END), auto-scroll enabled\n");
                }

                // Regular text input
                int key = GetCharPressed();
                bool textChanged = false;
                while (key > 0) {
                    if ((key >= 32) && (key <= 126)) {
                        if (!textChanged) {
                            pushUndo(&modal);
                            textChanged = true;
                        }
                        char ch[2] = {(char)key, '\0'};
                        insertTextAtCursor(&modal, ch);
                    }
                    key = GetCharPressed();
                }

                // Handle Enter for new line
                if (IsKeyPressed(KEY_ENTER)) {
                    pushUndo(&modal);
                    insertTextAtCursor(&modal, "\n");
                }

                // Handle Backspace
                if (IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) {
                    if (modal.commandLength > 0 || modal.hasSelection) {
                        pushUndo(&modal);
                        deleteCharAtCursor(&modal, true);
                    }
                }

                // Handle Delete
                if (IsKeyPressed(KEY_DELETE) || IsKeyPressedRepeat(KEY_DELETE)) {
                    if (modal.commandLength > 0 || modal.hasSelection) {
                        pushUndo(&modal);
                        deleteCharAtCursor(&modal, false);
                    }
                }

                // Auto-scroll to cursor ONLY if not manually scrolling
                if (!modal.isManualScrolling) {
                    int line, col;
                    getCursorLineCol(modal.command, modal.cursorPos, &line, &col);
                    int lineHeight = fontSize + 4;
                    float cursorY = line * lineHeight;

                    // Calculate scroll with margin
                    float topMargin = 20;
                    float bottomMargin = 280 - lineHeight - 20;

                    bool needsScroll = false;
                    if (cursorY < modal.commandScrollOffsetY + topMargin) {
                        modal.commandScrollOffsetY = cursorY - topMargin;
                        if (modal.commandScrollOffsetY < 0) modal.commandScrollOffsetY = 0;
                        needsScroll = true;
                    } else if (cursorY > modal.commandScrollOffsetY + bottomMargin) {
                        modal.commandScrollOffsetY = cursorY - bottomMargin;
                        needsScroll = true;
                    }

                    // Clamp scroll
                    if (modal.commandScrollOffsetY < 0) modal.commandScrollOffsetY = 0;
                    if (modal.commandScrollOffsetY > modal.commandMaxScrollY) {
                        modal.commandScrollOffsetY = modal.commandMaxScrollY;
                    }

                    if (needsScroll) {
                        printf("[SCROLL] Auto-scrolled to cursor - line: %d, offset: %.2f\n", line, modal.commandScrollOffsetY);
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
                        10, GetScreenHeight() - 28, 16, (Color){98, 114, 164, 164});
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

            // Count lines for line numbers
            int lineCount = 1;
            for (int i = 0; i < modal.commandLength; i++) {
                if (modal.command[i] == '\n') lineCount++;
            }

            // Calculate content height for scrolling
            int lineHeight = fontSize + 4;
            float contentHeight = lineCount * lineHeight;
            modal.commandMaxScrollY = contentHeight - 290;
            if (modal.commandMaxScrollY < 0) modal.commandMaxScrollY = 0;

            // Clamp scroll offset
            if (modal.commandScrollOffsetY < 0) modal.commandScrollOffsetY = 0;
            if (modal.commandScrollOffsetY > modal.commandMaxScrollY) modal.commandScrollOffsetY = modal.commandMaxScrollY;

            // Use the enhanced DrawCommandWithLineNumbers function
            bool showCursor = modal.commandActive && ((modal.framesCounter / 20) % 2) == 0;
            DrawCommandWithLineNumbers(customFont, useCustomFont, modal.command, modal.commandBox,
                                     modal.commandScrollOffsetY, fontSize,
                                     modal.cursorPos, showCursor,
                                     modal.selectionStart, modal.selectionEnd, modal.hasSelection);

            // Draw vertical scrollbar
            if (modal.commandMaxScrollY > 0) {
                float scrollbarHeight = (290 / contentHeight) * 290;
                if (scrollbarHeight < 20) scrollbarHeight = 20;
                float scrollbarY = modalY + 160 + (modal.commandScrollOffsetY / modal.commandMaxScrollY) * (290 - scrollbarHeight);

                Rectangle scrollbarRect = {modalX + modalWidth - 30, scrollbarY, 6, scrollbarHeight};

                // Determine scrollbar color based on state
                Color scrollbarColor;
                if (modal.isDraggingScrollbar) {
                    scrollbarColor = (Color){139, 233, 253, 255}; // Bright cyan when dragging
                } else if (CheckCollisionPointRec(mousePoint, scrollbarRect)) {
                    scrollbarColor = (Color){139, 233, 253, 200}; // Lighter cyan on hover
                } else {
                    scrollbarColor = (Color){98, 114, 164, 255}; // Default color
                }

                DrawRectangle((int)scrollbarRect.x, (int)scrollbarRect.y, (int)scrollbarRect.width, (int)scrollbarRect.height, scrollbarColor);
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

            DrawTextCustom(customFont, useCustomFont, "Arrow Keys: Navigate | Shift+Arrows: Select | Ctrl+A: Select All", modalX + 20, modalY + modalHeight - 50, 13, (Color){98, 114, 164, 255});
            DrawTextCustom(customFont, useCustomFont, "Ctrl+Z/Y: Undo/Redo | Ctrl+C/X/V: Copy/Cut/Paste | Mouse: Click & Drag", modalX+20 , modalY + modalHeight - 30, 13, (Color){98, 114, 164, 255});
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
