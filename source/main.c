#include "common.h"

//--------Keys--------
int key_quit = KEY_ESCAPE;
int key_pause = KEY_SPACE;
int key_advance = KEY_LEFT_SHIFT;

int key_clear = KEY_R;
int key_zoom_in = KEY_Z;
int key_zoom_out = KEY_X;
int key_selection = KEY_C;
int key_print = KEY_V;

int key_move_right = KEY_D;
int key_move_left = KEY_A;
int key_move_down = KEY_S;
int key_move_up = KEY_W;

int key_cell_1 = KEY_ONE;
int key_cell_2 = KEY_TWO;
int key_cell_3 = KEY_THREE;

//--------Constants--------
int trueScreenWidth = 1920;
int trueScreenHeight = 1080;
int screenWidth;
int screenHeight;

int pixelSize = 8;
int fps = 120;
int fpu = 5;

UINT32 mousePosX;
UINT32 mousePosY;

//--------Vars--------
int updateCounter;

bool paused = true;
bool advance = false;
int pixelToPlace = 1;

UINT32 xPos = 0x7fffffff;
UINT32 yPos = 0x7fffffff;

//--------Cell Arrays--------
typedef struct
{
    UINT64 key;
    UINT8 type;
    UT_hash_handle hh;
} Cell;

Color pixelColors[4] = {(Color){0, 0, 96, 255}, (Color){120, 200, 255, 255}, (Color){220, 170, 20, 255}, (Color){0, 160, 115, 255}};
Color halfOpacityBG = {0, 0, 96, 127};
Color lineColor = {30, 50, 127, 255};
UINT8 pixelCount[4];
int pixelAmount = 4;

Cell *nonDeadCells;
Cell *deadCells;
Cell *deadToAdd;

//--------Selection--------
UINT8 selectionMode;
Cell *selection;
UINT8 printMode;

Color printColor = {255, 200, 30, 255};
Color selectionColor = {30, 200, 255, 255};

UINT32 selectionStartX;
UINT32 selectionEndX;
UINT32 selectionStartY;
UINT32 selectionEndY;
UINT32 selectionLeft;
UINT32 selectionTop;

//--------Cell Info Array--------
// FIRST NUMBER -> Pixel/Cell ID
// SECOND NUMBER -> Pixel/Cell Count
// RESULT -> Pixel/Cell ID to Become
UINT8 cellsArray[4][4][9] = {
    {
        // FROM 0
        [1][3] = 0x10,       // TO 1
        [2][3 ... 4] = 0x20, // TO 2
        [3][3] = 0x30        // TO 3
    },
    {
        // FROM 1
        [1][2 ... 3] = 0x10, // TO 1
        [2][2] = 0x10,       // TO 1
        [2][3 ... 4] = 0x20, // TO 2
        [3][2 ... 4] = 0x30  // TO 3
    },
    {
        // FROM 2
        [2][1] = 0x10,       // TO 1
        [2][3] = 0x20,       // TO 2
        [1][2 ... 4] = 0x20, // TO 2
        [3][2 ... 4] = 0x30  // TO 3
    },
    {
        // FROM 3
        [3][2] = 0x10,
        [1][3] = 0x10,       // TO 1
        [2][2 ... 3] = 0x20, // TO 2
        [3][3] = 0x30,       // TO 3
        [1][1 ... 2] = 0x30  // TO 3
    }};

//--------Functions--------
void updateCell(UINT32 cellX, UINT32 cellY, Cell *thisCell, UINT8 thisType)
{
    memset(pixelCount, 0, sizeof(pixelCount));
    for (int y = -1; y <= 1; y++)
    {
        for (int x = -1; x <= 1; x++)
        {
            if (!y && !x)
                continue;

            UINT64 cellKey = (UINT64)(cellX + x) + ((UINT64)(cellY + y) << 32);
            Cell *neighbor = (Cell *)malloc(sizeof(Cell));

            HASH_FIND(hh, nonDeadCells, &cellKey, sizeof(UINT64), neighbor);
            if (neighbor != NULL)
                pixelCount[neighbor->type & 15]++;
        }
    }

    UINT8 valueToChange = 0;
    for (int i = pixelAmount - 1; i > 0; i--)
        valueToChange = max(cellsArray[thisType][i][pixelCount[i]], valueToChange);

    thisCell->type |= valueToChange;
}

void addDeadNeighbors(UINT32 cellX, UINT32 cellY, bool addNOW)
{
    Cell **hashToAdd = &deadToAdd;
    if (addNOW)
        hashToAdd = &deadCells;

    for (int y = -1; y <= 1; y++)
    {
        for (int x = -1; x <= 1; x++)
        {
            if (!y && !x)
                continue;

            UINT64 cellKey = (UINT64)(cellX + x) + ((UINT64)(cellY + y) << 32);

            Cell *isInNotDead = (Cell *)malloc(sizeof(Cell));
            HASH_FIND(hh, nonDeadCells, &cellKey, sizeof(UINT64), isInNotDead);
            if (isInNotDead != NULL)
                continue;

            Cell *isInDead = (Cell *)malloc(sizeof(Cell));
            HASH_FIND(hh, deadCells, &cellKey, sizeof(UINT64), isInDead);
            if (isInDead != NULL)
                continue;

            Cell *isInDeadToAdd = (Cell *)malloc(sizeof(Cell));
            HASH_FIND(hh, deadToAdd, &cellKey, sizeof(UINT64), isInDeadToAdd);
            if (isInDeadToAdd != NULL)
                continue;

            Cell *newCell = (Cell *)malloc(sizeof(Cell));
            newCell->key = cellKey;
            newCell->type = 0;
            HASH_ADD(hh, *hashToAdd, key, sizeof(UINT64), newCell);
        }
    }
}

void doThePrinting(UINT8 printValue)
{
    selectionLeft = selectionStartX < selectionEndX ? selectionStartX : selectionEndX;
    selectionTop = selectionStartY < selectionEndY ? selectionStartY : selectionEndY;
    UINT32 endX = selectionStartX < selectionEndX ? selectionEndX : selectionStartX;
    UINT32 endY = selectionStartY < selectionEndY ? selectionEndY : selectionStartY;

    int imageWidth = (endX - selectionLeft + 1) * pixelSize;
    int imageHeight = (endY - selectionTop + 1) * pixelSize;

    Color *pixels = (Color *)malloc(imageWidth * imageHeight * sizeof(Color));
    for (UINT64 y = selectionTop; y <= endY; y++)
    {
        for (UINT64 x = selectionLeft; x <= endX; x++)
        {
            int pixelID = ((x - selectionLeft) + (y - selectionTop) * imageWidth) * pixelSize;

            UINT64 cellKey = x + (y << 32);
            Cell *foundCell = (Cell *)malloc(sizeof(Cell));
            HASH_FIND(hh, nonDeadCells, &cellKey, sizeof(UINT64), foundCell);
            if (foundCell == NULL)
            {
                for (int pY = 0; pY < pixelSize; pY++)
                {
                    for (int pX = 0; pX < pixelSize; pX++)
                    {
                        if ((pX == 0 || pX == pixelSize - 1 || pY == 0 || pY == pixelSize - 1) && pixelSize >= 4)
                            pixels[pixelID + pX + pY * imageWidth] = lineColor;
                        else
                            pixels[pixelID + pX + pY * imageWidth] = pixelColors[0];
                    }
                }
                continue;
            }

            for (int pY = 0; pY < pixelSize; pY++)
                for (int pX = 0; pX < pixelSize; pX++)
                    pixels[pixelID + pX + pY * imageWidth] = pixelColors[foundCell->type & 15];
        }
    }

    Image print;
    print.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
    print.width = imageWidth;
    print.height = imageHeight;
    print.mipmaps = 1;
    print.data = pixels;

    time_t t = time(NULL);
    struct tm tm = *localtime(&t);
    char timeName[64];
    strftime(timeName, sizeof(timeName), "%Y-%m-%d %H-%M-%S", &tm);

    char dir[256];
    sprintf(dir, "%sprints", GetApplicationDirectory());
    if (!DirectoryExists(dir))
        MakeDirectory(dir);

    char imageName[512];
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    sprintf(imageName, "%sprints\\%s-%03ld.png", GetApplicationDirectory(), timeName, ts.tv_nsec / 1000000);

    FILE *imageFile;
    imageFile = fopen(imageName, "w");
    fprintf(imageFile, ":D");
    fclose(imageFile);

    ExportImage(print, imageName);
    printMode = printValue;
    selectionMode = false;

    Cell *selectionCell, *tmpSelection;
    HASH_ITER(hh, selection, selectionCell, tmpSelection)
    {
        HASH_DEL(selection, selectionCell);
        free(selectionCell);
    }
}

void updateUI()
{
    Vector2 mousePos = GetMousePosition();
    mousePosX = (UINT32)floorf(mousePos.x / pixelSize);
    mousePosY = (UINT32)floorf(mousePos.y / pixelSize);

    screenWidth = (int)ceilf((float)trueScreenWidth / pixelSize);
    screenHeight = (int)ceilf((float)trueScreenHeight / pixelSize);

    if (IsKeyDown(key_move_right))
        xPos++;
    if (IsKeyDown(key_move_left))
        xPos--;
    if (IsKeyDown(key_move_down))
        yPos++;
    if (IsKeyDown(key_move_up))
        yPos--;

    float wheelMove = GetMouseWheelMove();
    if ((wheelMove < 0 || IsKeyPressed(key_zoom_in)) && pixelSize > 1)
        pixelSize--;
    else if (wheelMove > 0 || IsKeyPressed(key_zoom_out))
        pixelSize++;

    if (IsKeyPressed(key_pause))
        paused = !paused;
    if (IsKeyPressed(key_advance))
        advance = true;
    if (IsKeyPressed(key_selection))
    {
        selectionMode = !selectionMode;
        printMode = false;

        Cell *selectionCell, *tmpSelection;
        HASH_ITER(hh, selection, selectionCell, tmpSelection)
        {
            HASH_DEL(selection, selectionCell);
            free(selectionCell);
        }
    }
    if (IsKeyPressed(key_print))
    {
        selectionMode = 0;
        printMode = !printMode;

        Cell *selectionCell, *tmpSelection;
        HASH_ITER(hh, selection, selectionCell, tmpSelection)
        {
            HASH_DEL(selection, selectionCell);
            free(selectionCell);
        }
    }

    if (IsKeyPressed(key_cell_1))
        pixelToPlace = 1;
    else if (IsKeyPressed(key_cell_2))
        pixelToPlace = 2;
    else if (IsKeyPressed(key_cell_3))
        pixelToPlace = 3;

    if (IsKeyPressed(key_clear))
    {
        Cell *cell, *tmp;
        HASH_ITER(hh, nonDeadCells, cell, tmp)
        {
            HASH_DEL(nonDeadCells, cell);
            free(cell);
        }

        Cell *deadAdd, *tmpDeadAdd;
        HASH_ITER(hh, deadToAdd, deadAdd, tmpDeadAdd)
        {
            HASH_DEL(deadToAdd, deadAdd);
            free(deadAdd);
        }

        Cell *cellDead, *tmpDead;
        HASH_ITER(hh, deadCells, cellDead, tmpDead)
        {
            HASH_DEL(deadCells, cellDead);
            free(cellDead);
        }
    }

    if (selectionMode || printMode)
    {
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        {
            UINT32 cellOffsetX = mousePosX + xPos - selectionLeft - (INT64)abs((INT64)selectionLeft - (INT64)selectionEndX);
            UINT32 cellOffsetY = mousePosY + yPos - selectionTop - (INT64)abs((INT64)selectionTop - (INT64)selectionEndY);

            Cell *selectionCell, *tmpSelection;
            HASH_ITER(hh, selection, selectionCell, tmpSelection)
            {
                Cell *newCell = (Cell *)malloc(sizeof(Cell));
                newCell->key = (UINT32)((selectionCell->key + cellOffsetX) & 0xffffffff) + ((((selectionCell->key >> 32) & 0xffffffff) + cellOffsetY) << 32);
                newCell->type = selectionCell->type;
                HASH_ADD(hh, nonDeadCells, key, sizeof(UINT64), newCell);

                Cell *deadCheck = (Cell *)malloc(sizeof(Cell));
                HASH_FIND(hh, deadCells, &newCell->key, sizeof(UINT64), deadCheck);
                if (deadCheck != NULL)
                    HASH_DEL(deadCells, deadCheck);

                addDeadNeighbors((UINT32)(selectionCell->key + cellOffsetX) & 0xffffffff, ((selectionCell->key >> 32) & 0xffffffff) + cellOffsetY, true);
            }
        }

        if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) || (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && printMode))
        {
            selectionMode = 2;

            selectionStartX = mousePosX + xPos;
            selectionStartY = mousePosY + yPos;

            Cell *selectionCell, *tmpSelection;
            HASH_ITER(hh, selection, selectionCell, tmpSelection)
            {
                HASH_DEL(selection, selectionCell);
                free(selectionCell);
            }
        }

        if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT) || (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && printMode))
        {
            selectionEndX = mousePosX + xPos;
            selectionEndY = mousePosY + yPos;

            selectionLeft = selectionStartX < selectionEndX ? selectionStartX : selectionEndX;
            selectionTop = selectionStartY < selectionEndY ? selectionStartY : selectionEndY;
        }

        if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && printMode)
        {
            doThePrinting(1);
            return;
        }

        if (IsMouseButtonReleased(MOUSE_BUTTON_RIGHT))
        {
            if (selectionStartX == selectionEndX && selectionStartY == selectionEndY)
            {
                if (selectionMode)
                    selectionMode = 1;
                else if (printMode)
                    printMode = 1;
                return;
            }

            if (printMode)
            {
                doThePrinting(2);
                return;
            }

            selectionMode = 3;

            selectionLeft = selectionStartX < selectionEndX ? selectionStartX : selectionEndX;
            selectionTop = selectionStartY < selectionEndY ? selectionStartY : selectionEndY;
            UINT32 endX = selectionStartX < selectionEndX ? selectionEndX : selectionStartX;
            UINT32 endY = selectionStartY < selectionEndY ? selectionEndY : selectionStartY;

            for (UINT64 y = selectionTop; y <= endY; y++)
            {
                for (UINT64 x = selectionLeft; x <= endX; x++)
                {
                    UINT64 cellKey = x + (y << 32);
                    Cell *foundCell = (Cell *)malloc(sizeof(Cell));
                    HASH_FIND(hh, nonDeadCells, &cellKey, sizeof(UINT64), foundCell);
                    if (foundCell == NULL)
                        continue;

                    Cell *selectionCell = (Cell *)malloc(sizeof(Cell));
                    selectionCell->key = foundCell->key;
                    selectionCell->type = foundCell->type & 15;
                    HASH_ADD(hh, selection, key, sizeof(UINT64), selectionCell);
                }
            }
        }
        return;
    }

    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT))
    {
        UINT32 xPixelPos = mousePosX + xPos;
        UINT32 yPixelPos = mousePosY + yPos;
        UINT64 cellKey = (UINT64)xPixelPos + ((UINT64)yPixelPos << 32);

        Cell *oldCell = (Cell *)malloc(sizeof(Cell));
        HASH_FIND(hh, nonDeadCells, &cellKey, sizeof(UINT64), oldCell);

        if (oldCell != NULL)
            oldCell->type = pixelToPlace;
        else
        {
            Cell *newCell = (Cell *)malloc(sizeof(Cell));
            newCell->key = cellKey;
            newCell->type = pixelToPlace;
            HASH_ADD(hh, nonDeadCells, key, sizeof(UINT64), newCell);

            Cell *deadCheck = (Cell *)malloc(sizeof(Cell));
            HASH_FIND(hh, deadCells, &cellKey, sizeof(UINT64), deadCheck);
            if (deadCheck != NULL)
                HASH_DEL(deadCells, deadCheck);

            addDeadNeighbors(xPixelPos, yPixelPos, true);
        }
    }
    else if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT))
    {
        UINT32 xPixelPos = mousePosX + xPos;
        UINT32 yPixelPos = mousePosY + yPos;

        UINT64 cellKey = (UINT64)xPixelPos + ((UINT64)yPixelPos << 32);
        Cell *oldCell = (Cell *)malloc(sizeof(Cell));
        HASH_FIND(hh, nonDeadCells, &cellKey, sizeof(UINT64), oldCell);
        if (oldCell != NULL)
            HASH_DEL(nonDeadCells, oldCell);

        Cell *deadCell = (Cell *)malloc(sizeof(Cell));
        HASH_FIND(hh, deadCells, &cellKey, sizeof(UINT64), deadCell);
        if (deadCell == NULL)
        {
            Cell *newDeadCell = (Cell *)malloc(sizeof(Cell));
            newDeadCell->key = cellKey;
            newDeadCell->type = 0;
            HASH_ADD(hh, deadCells, key, sizeof(UINT64), newDeadCell);
        }
    }
}

void updateGame()
{
    if (!advance)
    {
        if (paused)
            return;

        updateCounter--;
        if (updateCounter > 0)
            return;
    }

    Cell *cell, *tmp;
    HASH_ITER(hh, nonDeadCells, cell, tmp)
    {
        UINT32 cellX = (UINT32)(cell->key & 0xffffffff);
        UINT32 cellY = (UINT32)((cell->key >> 32) & 0xffffffff);
        updateCell(cellX, cellY, cell, cell->type & 15);
    }

    Cell *deadAdd, *tmpDeadAdd;
    HASH_ITER(hh, deadToAdd, deadAdd, tmpDeadAdd)
    {
        Cell *newDeadCell = (Cell *)malloc(sizeof(Cell));
        newDeadCell->key = deadAdd->key;
        newDeadCell->type = 0;

        HASH_ADD(hh, deadCells, key, sizeof(UINT64), newDeadCell);
        HASH_DEL(deadToAdd, deadAdd);
        free(deadAdd);

        UINT32 cellX = (UINT32)(newDeadCell->key & 0xffffffff) + xPos;
        UINT32 cellY = (UINT32)((newDeadCell->key >> 32) & 0xffffffff) + yPos;
        updateCell(cellX, cellY, newDeadCell, 0);
    }

    Cell *cellDead, *tmpDead;
    HASH_ITER(hh, deadCells, cellDead, tmpDead)
    {
        UINT32 cellX = (UINT32)(cellDead->key & 0xffffffff);
        UINT32 cellY = (UINT32)((cellDead->key >> 32) & 0xffffffff);
        updateCell(cellX, cellY, cellDead, 0);
    }

    Cell *cellUpdate, *tmpUpdate;
    HASH_ITER(hh, nonDeadCells, cellUpdate, tmpUpdate)
    {
        cellUpdate->type >>= 4;

        if (cellUpdate->type)
            continue;

        Cell *newDeadCell = (Cell *)malloc(sizeof(Cell));
        newDeadCell->key = cellUpdate->key;
        newDeadCell->type = 0;

        HASH_ADD(hh, deadCells, key, sizeof(UINT64), newDeadCell);
        HASH_DEL(nonDeadCells, cellUpdate);
    }

    Cell *cellDeadUpdate, *tmpDeadUpdate;
    HASH_ITER(hh, deadCells, cellDeadUpdate, tmpDeadUpdate)
    {
        cellDeadUpdate->type >>= 4;

        if (!cellDeadUpdate->type)
        {
            bool nonDeadNeighbors = false;
            for (int y = -1; y <= 1; y++)
            {
                for (int x = -1; x <= 1; x++)
                {
                    if (!y && !x)
                        continue;

                    UINT64 cellKey = cellDeadUpdate->key + x + ((UINT64)y << 32);
                    Cell *neighbor = (Cell *)malloc(sizeof(Cell));

                    HASH_FIND(hh, nonDeadCells, &cellKey, sizeof(UINT64), neighbor);
                    if (neighbor != NULL)
                        nonDeadNeighbors = true;
                }
            }

            if (!nonDeadNeighbors)
                HASH_DEL(deadCells, cellDeadUpdate);
        }
        else
        {
            Cell *newNonDeadCell = (Cell *)malloc(sizeof(Cell));
            newNonDeadCell->key = cellDeadUpdate->key;
            newNonDeadCell->type = cellDeadUpdate->type;

            HASH_ADD(hh, nonDeadCells, key, sizeof(UINT64), newNonDeadCell);
            HASH_DEL(deadCells, cellDeadUpdate);

            UINT32 cellX = (UINT32)(newNonDeadCell->key & 0xffffffff);
            UINT32 cellY = (UINT32)((newNonDeadCell->key >> 32) & 0xffffffff);
            addDeadNeighbors(cellX, cellY, true);
        }
    }

    if (advance && printMode == 2)
        doThePrinting(2);

    advance = false;
    updateCounter = fpu;
}

void drawGame()
{
    ClearBackground(pixelColors[0]);

    Cell *selectionCell, *tmpSelection;
    HASH_ITER(hh, selection, selectionCell, tmpSelection)
    {
        UINT32 cellX = (UINT32)(selectionCell->key & 0xffffffff);
        int cellDrawX = cellX + mousePosX - selectionLeft - (INT64)abs((INT64)selectionLeft - (INT64)selectionEndX);
        if (cellDrawX < 0 || cellDrawX > screenWidth)
            continue;

        UINT32 cellY = (UINT32)((selectionCell->key >> 32) & 0xffffffff);
        int cellDrawY = cellY + mousePosY - selectionTop - (INT64)abs((INT64)selectionTop - (INT64)selectionEndY);
        if (cellDrawY < 0 || cellDrawY > screenHeight)
            continue;

        UINT8 cellType = selectionCell->type & 15;
        DrawRectangle(cellDrawX * pixelSize, cellDrawY * pixelSize, pixelSize, pixelSize, pixelColors[cellType]);
    }

    DrawRectangle(0, 0, trueScreenWidth, trueScreenHeight, halfOpacityBG);

    if (pixelSize >= 4)
    {
        int thicc = 1;

        for (int x = 0; x < trueScreenWidth; x += pixelSize)
        {
            DrawRectangle(x, 0, thicc, trueScreenHeight, lineColor);
            DrawRectangle(x + pixelSize - thicc, 0, thicc, trueScreenHeight, lineColor);
        }

        for (int y = 0; y < trueScreenHeight; y += pixelSize)
        {
            DrawRectangle(0, y, trueScreenWidth, thicc, lineColor);
            DrawRectangle(0, y + pixelSize - thicc, trueScreenWidth, thicc, lineColor);
        }
    }

    Cell *cell, *tmp;
    HASH_ITER(hh, nonDeadCells, cell, tmp)
    {
        UINT32 cellX = (UINT32)(cell->key & 0xffffffff);
        if (cellX - xPos < 0 || cellX - xPos > screenWidth)
            continue;

        UINT32 cellY = (UINT32)((cell->key >> 32) & 0xffffffff);
        if (cellY - yPos < 0 || cellY - yPos > screenHeight)
            continue;

        UINT8 cellType = cell->type & 15;
        DrawRectangle((cellX - xPos) * pixelSize, (cellY - yPos) * pixelSize, pixelSize, pixelSize, pixelColors[cellType]);
    }

    if (selectionMode >= 2 || printMode >= 2)
    {
        Color colorToUse = selectionColor;
        if (printMode)
            colorToUse = printColor;

        int boxThicc = pixelSize / 3;
        if (boxThicc < 1)
            boxThicc = 1;

        INT64 xBoxOffset = -(INT64)xPos;
        INT64 yBoxOffset = -(INT64)yPos;
        if (selectionMode == 3)
        {
            xBoxOffset = (INT64)mousePosX - (INT64)selectionLeft - (INT64)abs((INT64)selectionLeft - (INT64)selectionEndX);
            yBoxOffset = (INT64)mousePosY - (INT64)selectionTop - (INT64)abs((INT64)selectionTop - (INT64)selectionEndY);
        }

        INT64 boxXStart = ((INT64)selectionLeft + xBoxOffset) * pixelSize;
        INT64 boxYStart = ((INT64)selectionTop + yBoxOffset) * pixelSize;
        int boxWidth = (abs((INT64)selectionStartX - (INT64)selectionEndX) + 1) * pixelSize;
        int boxHeight = (abs((INT64)selectionStartY - (INT64)selectionEndY) + 1) * pixelSize;

        DrawRectangle(boxXStart, boxYStart, boxWidth, boxThicc, colorToUse);
        DrawRectangle(boxXStart, boxYStart, boxThicc, boxHeight, colorToUse);
        DrawRectangle(boxXStart, boxYStart + boxHeight - boxThicc, boxWidth, boxThicc, colorToUse);
        DrawRectangle(boxXStart + boxWidth - boxThicc, boxYStart, boxThicc, boxHeight, colorToUse);
    }

    char info1[128];
    sprintf(info1, "XY <%lld, %lld>", (INT64)xPos - 0x7fffffff, (INT64)yPos - 0x7fffffff);
    DrawText(info1, 8, 8, 24, WHITE);

    char info2[128];
    sprintf(info2, "Zoom: %d     FPS: %d", pixelSize, GetFPS());
    DrawText(info2, 8, 48, 24, WHITE);

    char selectedInfo[16];
    sprintf(selectedInfo, "%d", pixelToPlace);
    DrawText("Selected Cell:", 8, 88, 24, WHITE);
    DrawText(selectedInfo, 178, 80, 40, pixelColors[pixelToPlace]);

    char pausedInfo[64];
    sprintf(pausedInfo, "%s", paused ? "PAUSED" : "RUNNING");
    DrawText(pausedInfo, 8, 128, 24, paused ? RED : GREEN);

    char selectionInfo[128];
    sprintf(selectionInfo, "%s MODE", printMode ? "PRINT" : (selectionMode ? "SELECTION" : "DRAWING"));
    DrawText(selectionInfo, 128, 128, 24, printMode ? printColor : (selectionMode ? selectionColor : (Color){255, 30, 200, 255}));
}

int main()
{
    SetConfigFlags(FLAG_WINDOW_UNDECORATED);
    InitWindow(trueScreenWidth, trueScreenHeight, "Game of Life 2");

    trueScreenWidth = GetMonitorWidth(GetCurrentMonitor());
    trueScreenHeight = GetMonitorHeight(GetCurrentMonitor());
    pixelSize = trueScreenHeight / 135;

    SetWindowSize(trueScreenWidth, trueScreenHeight);
    SetWindowPosition(0, 0);
    SetTargetFPS(fps);
    SetExitKey(key_quit);

    while (!WindowShouldClose())
    {
        BeginDrawing();

        // Main Logic
        updateUI();
        updateGame();
        drawGame();

        EndDrawing();
    }

    CloseWindow();
    return 0;
}