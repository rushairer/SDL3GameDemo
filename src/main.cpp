/*
 * 贪吃蛇游戏的逻辑实现
 * 本代码采用高效的内存表示方式，使用位操作来存储游戏状态
 *
 * 整体架构：
 * 1. 使用SDL3作为图形渲染引擎
 * 2. 采用回调方式处理游戏主循环
 * 3. 状态管理和渲染分离设计
 */

#define SDL_MAIN_USE_CALLBACKS 1 /* 使用回调方式替代main函数 */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

/* 游戏基本参数设置 */
#define STEP_RATE_IN_MILLISECONDS 125 /* 游戏更新时间步长（毫秒） */
#define SNAKE_BLOCK_SIZE_IN_PIXELS 24 /* 蛇身方块大小（像素） */
#define SDL_WINDOW_WIDTH (SNAKE_BLOCK_SIZE_IN_PIXELS * SNAKE_GAME_WIDTH)
#define SDL_WINDOW_HEIGHT (SNAKE_BLOCK_SIZE_IN_PIXELS * SNAKE_GAME_HEIGHT)

/* 游戏场地大小设置 */
#define SNAKE_GAME_WIDTH 24U  /* 游戏场地宽度（格子数） */
#define SNAKE_GAME_HEIGHT 18U /* 游戏场地高度（格子数） */
#define SNAKE_MATRIX_SIZE (SNAKE_GAME_WIDTH * SNAKE_GAME_HEIGHT)

/* 位操作相关的常量定义 */
#define THREE_BITS 0x7U /* 用于位操作的3位掩码，用于提取单元格状态 */
#define SHIFT(x, y) (((x) + ((y) * SNAKE_GAME_WIDTH)) * SNAKE_CELL_MAX_BITS)

/* 单元格状态枚举
 * 使用3位二进制表示不同的单元格状态
 * 0: 空单元格
 * 1-4: 蛇身体（不同方向）
 * 5: 食物
 */
typedef enum
{
    SNAKE_CELL_NOTHING = 0U, /* 空单元格 */
    SNAKE_CELL_SRIGHT = 1U,  /* 蛇身体向右 */
    SNAKE_CELL_SUP = 2U,     /* 蛇身体向上 */
    SNAKE_CELL_SLEFT = 3U,   /* 蛇身体向左 */
    SNAKE_CELL_SDOWN = 4U,   /* 蛇身体向下 */
    SNAKE_CELL_FOOD = 5U     /* 食物 */
} SnakeCell;

#define SNAKE_CELL_MAX_BITS 3U /* 表示一个单元格状态所需的位数 */

/* 蛇的移动方向枚举 */
typedef enum
{
    SNAKE_DIR_RIGHT, /* 向右移动 */
    SNAKE_DIR_UP,    /* 向上移动 */
    SNAKE_DIR_LEFT,  /* 向左移动 */
    SNAKE_DIR_DOWN   /* 向下移动 */
} SnakeDirection;

/* 蛇的状态上下文结构
 * 使用位压缩存储游戏场地状态，每个单元格用3位表示
 */
typedef struct
{
    unsigned char cells[(SNAKE_MATRIX_SIZE * SNAKE_CELL_MAX_BITS) / 8U]; /* 游戏场地状态数组 */
    char head_xpos;           /* 蛇头X坐标 */
    char head_ypos;           /* 蛇头Y坐标 */
    char tail_xpos;           /* 蛇尾X坐标 */
    char tail_ypos;           /* 蛇尾Y坐标 */
    char next_dir;            /* 下一步移动方向 */
    char inhibit_tail_step;   /* 抑制蛇尾移动的计数器（用于实现蛇身增长） */
    unsigned occupied_cells;   /* 已占用的单元格数量 */
} SnakeContext;

/* 应用程序状态结构 */
typedef struct
{
    SDL_Window *window;      /* SDL窗口对象 */
    SDL_Renderer *renderer;   /* SDL渲染器对象 */
    SnakeContext snake_ctx;   /* 蛇的游戏状态 */
    Uint64 last_step;         /* 上一次更新的时间戳 */
} AppState;

/* 获取指定位置的单元格状态
 * 使用位操作从压缩存储中提取单元格信息
 */
SnakeCell snake_cell_at(const SnakeContext *ctx, char x, char y)
{
    const int shift = SHIFT(x, y);
    unsigned short range;
    SDL_memcpy(&range, ctx->cells + (shift / 8), sizeof(range));
    return (SnakeCell)((range >> (shift % 8)) & THREE_BITS);
}

/* 设置矩形的屏幕坐标
 * 将游戏坐标转换为屏幕像素坐标
 */
static void set_rect_xy_(SDL_FRect *r, short x, short y)
{
    r->x = (float)(x * SNAKE_BLOCK_SIZE_IN_PIXELS);
    r->y = (float)(y * SNAKE_BLOCK_SIZE_IN_PIXELS);
}

/* 设置指定位置的单元格状态
 * 使用位操作更新压缩存储中的单元格信息
 */
static void put_cell_at_(SnakeContext *ctx, char x, char y, SnakeCell ct)
{
    const int shift = SHIFT(x, y);
    const int adjust = shift % 8;
    unsigned char *const pos = ctx->cells + (shift / 8);
    unsigned short range;
    SDL_memcpy(&range, pos, sizeof(range));
    range &= ~(THREE_BITS << adjust); /* 清除原有状态 */
    range |= (ct & THREE_BITS) << adjust; /* 设置新状态 */
    SDL_memcpy(pos, &range, sizeof(range));
}

/* 检查游戏场地是否已满 */
static int are_cells_full_(SnakeContext *ctx)
{
    return ctx->occupied_cells == SNAKE_GAME_WIDTH * SNAKE_GAME_HEIGHT;
}

/* 在空闲位置生成新的食物
 * 使用随机数选择位置，确保不与蛇身重叠
 */
static void new_food_pos_(SnakeContext *ctx)
{
    while (true)
    {
        const char x = (char)SDL_rand(SNAKE_GAME_WIDTH);
        const char y = (char)SDL_rand(SNAKE_GAME_HEIGHT);
        if (snake_cell_at(ctx, x, y) == SNAKE_CELL_NOTHING)
        {
            put_cell_at_(ctx, x, y, SNAKE_CELL_FOOD);
            break;
        }
    }
}

/* 游戏初始化函数
 * 设置蛇的初始状态和位置，生成初始食物
 */
void snake_initialize(SnakeContext *ctx)
{
    int i;
    SDL_zeroa(ctx->cells);
    /* 设置蛇的初始位置（中心点） */
    ctx->head_xpos = ctx->tail_xpos = SNAKE_GAME_WIDTH / 2;
    ctx->head_ypos = ctx->tail_ypos = SNAKE_GAME_HEIGHT / 2;
    ctx->next_dir = SNAKE_DIR_RIGHT; /* 初始移动方向为右 */
    ctx->inhibit_tail_step = ctx->occupied_cells = 4;
    --ctx->occupied_cells;
    put_cell_at_(ctx, ctx->tail_xpos, ctx->tail_ypos, SNAKE_CELL_SRIGHT);
    /* 生成初始食物 */
    for (i = 0; i < 4; i++)
    {
        new_food_pos_(ctx);
        ++ctx->occupied_cells;
    }
}

/* 改变蛇的移动方向
 * 检查是否允许改变方向（不允许180度转弯）
 */
void snake_redir(SnakeContext *ctx, SnakeDirection dir)
{
    SnakeCell ct = snake_cell_at(ctx, ctx->head_xpos, ctx->head_ypos);
    /* 检查是否允许改变方向（不允许180度转弯） */
    if ((dir == SNAKE_DIR_RIGHT && ct != SNAKE_CELL_SLEFT) ||
        (dir == SNAKE_DIR_UP && ct != SNAKE_CELL_SDOWN) ||
        (dir == SNAKE_DIR_LEFT && ct != SNAKE_CELL_SRIGHT) ||
        (dir == SNAKE_DIR_DOWN && ct != SNAKE_CELL_SUP))
    {
        ctx->next_dir = dir;
    }
}

/* 处理坐标环绕（穿墙）
 * 当坐标超出边界时进行环绕处理
 */
static void wrap_around_(char *val, char max)
{
    if (*val < 0)
    {
        *val = max - 1;
    }
    else if (*val > max - 1)
    {
        *val = 0;
    }
}

/* 更新蛇的状态
 * 处理蛇的移动、碰撞检测和食物收集
 */
void snake_step(SnakeContext *ctx)
{
    const SnakeCell dir_as_cell = (SnakeCell)(ctx->next_dir + 1);
    SnakeCell ct;
    char prev_xpos;
    char prev_ypos;
    /* 移动蛇尾 */
    if (--ctx->inhibit_tail_step == 0)
    {
        ++ctx->inhibit_tail_step;
        ct = snake_cell_at(ctx, ctx->tail_xpos, ctx->tail_ypos);
        put_cell_at_(ctx, ctx->tail_xpos, ctx->tail_ypos, SNAKE_CELL_NOTHING);
        switch (ct)
        {
        case SNAKE_CELL_SRIGHT:
            ctx->tail_xpos++;
            break;
        case SNAKE_CELL_SUP:
            ctx->tail_ypos--;
            break;
        case SNAKE_CELL_SLEFT:
            ctx->tail_xpos--;
            break;
        case SNAKE_CELL_SDOWN:
            ctx->tail_ypos++;
            break;
        default:
            break;
        }
        wrap_around_(&ctx->tail_xpos, SNAKE_GAME_WIDTH);
        wrap_around_(&ctx->tail_ypos, SNAKE_GAME_HEIGHT);
    }
    /* 移动蛇头 */
    prev_xpos = ctx->head_xpos;
    prev_ypos = ctx->head_ypos;
    switch (ctx->next_dir)
    {
    case SNAKE_DIR_RIGHT:
        ++ctx->head_xpos;
        break;
    case SNAKE_DIR_UP:
        --ctx->head_ypos;
        break;
    case SNAKE_DIR_LEFT:
        --ctx->head_xpos;
        break;
    case SNAKE_DIR_DOWN:
        ++ctx->head_ypos;
        break;
    }
    wrap_around_(&ctx->head_xpos, SNAKE_GAME_WIDTH);
    wrap_around_(&ctx->head_ypos, SNAKE_GAME_HEIGHT);
    /* 碰撞检测 */
    ct = snake_cell_at(ctx, ctx->head_xpos, ctx->head_ypos);
    if (ct != SNAKE_CELL_NOTHING && ct != SNAKE_CELL_FOOD)
    {
        snake_initialize(ctx); /* 碰到蛇身，游戏重置 */
        return;
    }
    put_cell_at_(ctx, prev_xpos, prev_ypos, dir_as_cell);
    put_cell_at_(ctx, ctx->head_xpos, ctx->head_ypos, dir_as_cell);
    if (ct == SNAKE_CELL_FOOD)
    {
        if (are_cells_full_(ctx))
        {
            snake_initialize(ctx); /* 游戏胜利，重置游戏 */
            return;
        }
        new_food_pos_(ctx);        /* 生成新的食物 */
        ++ctx->inhibit_tail_step;  /* 延迟蛇尾移动，实现蛇身增长 */
        ++ctx->occupied_cells;
    }
}

/* 处理键盘事件
 * 包括游戏控制和蛇的方向控制
 */
static SDL_AppResult handle_key_event_(SnakeContext *ctx, SDL_Scancode key_code)
{
    switch (key_code)
    {
    /* 退出游戏 */
    case SDL_SCANCODE_ESCAPE:
    case SDL_SCANCODE_Q:
        return SDL_APP_SUCCESS;
    /* 重新开始游戏 */
    case SDL_SCANCODE_R:
        snake_initialize(ctx);
        break;
    /* 控制蛇的移动方向 */
    case SDL_SCANCODE_RIGHT:
        snake_redir(ctx, SNAKE_DIR_RIGHT);
        break;
    case SDL_SCANCODE_UP:
        snake_redir(ctx, SNAKE_DIR_UP);
        break;
    case SDL_SCANCODE_LEFT:
        snake_redir(ctx, SNAKE_DIR_LEFT);
        break;
    case SDL_SCANCODE_DOWN:
        snake_redir(ctx, SNAKE_DIR_DOWN);
        break;
    default:
        break;
    }
    return SDL_APP_CONTINUE;
}

/* 游戏主循环更新函数
 * 处理游戏状态更新和画面渲染
 */
SDL_AppResult SDL_AppIterate(void *appstate)
{
    AppState *as = (AppState *)appstate;
    SnakeContext *ctx = &as->snake_ctx;
    const Uint64 now = SDL_GetTicks();
    SDL_FRect r;
    unsigned i;
    unsigned j;
    int ct;

    /* 根据时间步长更新游戏状态 */
    while ((now - as->last_step) >= STEP_RATE_IN_MILLISECONDS)
    {
        snake_step(ctx);
        as->last_step += STEP_RATE_IN_MILLISECONDS;
    }

    /* 渲染游戏画面 */
    r.w = r.h = SNAKE_BLOCK_SIZE_IN_PIXELS;
    SDL_SetRenderDrawColor(as->renderer, 0, 0, 0, SDL_ALPHA_OPAQUE); /* 设置背景色为黑色 */
    SDL_RenderClear(as->renderer);

    /* 遍历并渲染游戏场地 */
    for (i = 0; i < SNAKE_GAME_WIDTH; i++)
    {
        for (j = 0; j < SNAKE_GAME_HEIGHT; j++)
        {
            ct = snake_cell_at(ctx, i, j);
            if (ct == SNAKE_CELL_NOTHING)
                continue;
            set_rect_xy_(&r, i, j);
            if (ct == SNAKE_CELL_FOOD)
                SDL_SetRenderDrawColor(as->renderer, 80, 80, 255, SDL_ALPHA_OPAQUE); /* 食物为蓝色 */
            else                                                                     /* body */
                SDL_SetRenderDrawColor(as->renderer, 0, 128, 0, SDL_ALPHA_OPAQUE);   /* 蛇身为绿色 */
            SDL_RenderFillRect(as->renderer, &r);
        }
    }

    /* 渲染蛇头（黄色） */
    SDL_SetRenderDrawColor(as->renderer, 255, 255, 0, SDL_ALPHA_OPAQUE);
    set_rect_xy_(&r, ctx->head_xpos, ctx->head_ypos);
    SDL_RenderFillRect(as->renderer, &r);
    SDL_RenderPresent(as->renderer);
    return SDL_APP_CONTINUE;
}

/* 游戏元数据信息 */
static const struct
{
    const char *key;
    const char *value;
} extended_metadata[] =
    {
        {SDL_PROP_APP_METADATA_URL_STRING, "https://examples.libsdl.org/SDL3/demo/01-snake/"},
        {SDL_PROP_APP_METADATA_CREATOR_STRING, "SDL team"},
        {SDL_PROP_APP_METADATA_COPYRIGHT_STRING, "Placed in the public domain"},
        {SDL_PROP_APP_METADATA_TYPE_STRING, "game"}};

/* 游戏初始化回调函数
 * 设置游戏元数据、初始化SDL、创建窗口和渲染器
 */
SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[])
{
    size_t i;

    /* 设置应用程序元数据 */
    if (!SDL_SetAppMetadata("Example Snake game", "1.0", "com.example.Snake"))
    {
        return SDL_APP_FAILURE;
    }

    /* 设置扩展元数据 */
    for (i = 0; i < SDL_arraysize(extended_metadata); i++)
    {
        if (!SDL_SetAppMetadataProperty(extended_metadata[i].key, extended_metadata[i].value))
        {
            return SDL_APP_FAILURE;
        }
    }

    /* 初始化SDL视频子系统 */
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        return SDL_APP_FAILURE;
    }

    /* 分配应用程序状态内存 */
    AppState *as = (AppState *)SDL_calloc(1, sizeof(AppState));
    if (!as)
    {
        return SDL_APP_FAILURE;
    }

    *appstate = as;

    /* 创建窗口和渲染器 */
    if (!SDL_CreateWindowAndRenderer("examples/demo/snake", SDL_WINDOW_WIDTH, SDL_WINDOW_HEIGHT, 0, &as->window, &as->renderer))
    {
        return SDL_APP_FAILURE;
    }

    /* 初始化游戏状态 */
    snake_initialize(&as->snake_ctx);

    as->last_step = SDL_GetTicks();

    return SDL_APP_CONTINUE;
}

/* 事件处理回调函数
 * 处理退出事件和键盘输入
 */
SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event)
{
    SnakeContext *ctx = &((AppState *)appstate)->snake_ctx;
    switch (event->type)
    {
    case SDL_EVENT_QUIT:
        return SDL_APP_SUCCESS;
    case SDL_EVENT_KEY_DOWN:
        return handle_key_event_(ctx, event->key.scancode);
    }
    return SDL_APP_CONTINUE;
}

/* 游戏退出回调函数
 * 清理资源并释放内存
 */
void SDL_AppQuit(void *appstate, SDL_AppResult result)
{
    if (appstate != NULL)
    {
        AppState *as = (AppState *)appstate;
        SDL_DestroyRenderer(as->renderer);
        SDL_DestroyWindow(as->window);
        SDL_free(as);
    }
}