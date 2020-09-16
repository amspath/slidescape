
#include "common.h"
#include "platform.h"
#include "macos_main.h"
#include "stringutils.h"

#include "imgui.h"
#include "imgui_freetype.h"
#include "imgui_impl_osx.h"
#include "imgui_impl_opengl3.h"
#include <stdio.h>
#import <Cocoa/Cocoa.h>
#import <OpenGL/gl.h>
#import <OpenGL/glu.h>

//#include "viewer.h"

#include <sys/sysctl.h>

//-----------------------------------------------------------------------------------
// ImGuiExampleView
//-----------------------------------------------------------------------------------

@interface SlideviewerView : NSOpenGLView
{
    NSTimer*    animationTimer;
}
@end

@implementation SlideviewerView

-(void)animationTimerFired:(NSTimer*)timer
{
    [self setNeedsDisplay:YES];
}

-(void)setSwapInterval:(int)interval
{
	[[self openGLContext] setValues:&interval forParameter:NSOpenGLCPSwapInterval];
}

-(void)prepareOpenGL
{
    [super prepareOpenGL];

#ifndef DEBUG
    GLint swapInterval = 1;
    [[self openGLContext] setValues:&swapInterval forParameter:NSOpenGLCPSwapInterval];
    if (swapInterval == 0)
        NSLog(@"Error: Cannot set swap interval.");
#endif
}

-(void)updateAndDrawDemoView
{
    // Start the Dear ImGui frame
	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplOSX_NewFrame(self);
    ImGui::NewFrame();

    // Global data for the demo
    static bool show_demo_window = true;
    static bool show_another_window = false;
    static ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    // 1. Show the big demo window (Most of the sample code is in ImGui::ShowDemoWindow()! You can browse its code to learn more about Dear ImGui!).
    if (show_demo_window)
        ImGui::ShowDemoWindow(&show_demo_window);

    // 2. Show a simple window that we create ourselves. We use a Begin/End pair to created a named window.
    {
        static float f = 0.0f;
        static int counter = 0;

        ImGui::Begin("Hello, world!");                          // Create a window called "Hello, world!" and append into it.

        ImGui::Text("This is some useful text.");               // Display some text (you can use a format strings too)
        ImGui::Checkbox("Demo Window", &show_demo_window);      // Edit bools storing our window open/close state
        ImGui::Checkbox("Another Window", &show_another_window);

        ImGui::SliderFloat("float", &f, 0.0f, 1.0f);            // Edit 1 float using a slider from 0.0f to 1.0f
        ImGui::ColorEdit3("clear color", (float*)&clear_color); // Edit 3 floats representing a color

        if (ImGui::Button("Button"))                            // Buttons return true when clicked (most widgets return true when edited/activated)
            counter++;
        ImGui::SameLine();
        ImGui::Text("counter = %d", counter);

        ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
        ImGui::End();
    }

    // 3. Show another simple window.
    if (show_another_window)
    {
        ImGui::Begin("Another Window", &show_another_window);   // Pass a pointer to our bool variable (the window will have a closing button that will clear the bool when clicked)
        ImGui::Text("Hello from another window!");
        if (ImGui::Button("Close Me"))
            show_another_window = false;
        ImGui::End();
    }

	// Rendering
	ImGui::Render();
	[[self openGLContext] makeCurrentContext];

    ImDrawData* draw_data = ImGui::GetDrawData();
    GLsizei width  = (GLsizei)(draw_data->DisplaySize.x * draw_data->FramebufferScale.x);
    GLsizei height = (GLsizei)(draw_data->DisplaySize.y * draw_data->FramebufferScale.y);
    glViewport(0, 0, width, height);

	glClearColor(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
	glClear(GL_COLOR_BUFFER_BIT);
	ImGui_ImplOpenGL3_RenderDrawData(draw_data);

	glFinish();

    // Present
    [[self openGLContext] flushBuffer];

    if (!animationTimer)
        animationTimer = [NSTimer scheduledTimerWithTimeInterval:0.017 target:self selector:@selector(animationTimerFired:) userInfo:nil repeats:YES];
}

-(void)reshape
{
	[super reshape];
	[[self openGLContext] update];
	[self updateAndDrawDemoView];
}

-(void)drawRect:(NSRect)bounds
{
    [self updateAndDrawDemoView];
}

-(BOOL)acceptsFirstResponder
{
    return (YES);
}

-(BOOL)becomeFirstResponder
{
    return (YES);
}

-(BOOL)resignFirstResponder
{
    return (YES);
}

-(void)dealloc
{
    animationTimer = nil;
    [super dealloc];
}

// Forward Mouse/Keyboard events to dear imgui OSX back-end. It returns true when imgui is expecting to use the event.
-(void)keyUp:(NSEvent *)event               { ImGui_ImplOSX_HandleEvent(event, self); }
-(void)keyDown:(NSEvent *)event             { ImGui_ImplOSX_HandleEvent(event, self); }
-(void)flagsChanged:(NSEvent *)event        { ImGui_ImplOSX_HandleEvent(event, self); }
-(void)mouseDown:(NSEvent *)event           { ImGui_ImplOSX_HandleEvent(event, self); }
-(void)rightMouseDown:(NSEvent *)event      { ImGui_ImplOSX_HandleEvent(event, self); }
-(void)otherMouseDown:(NSEvent *)event      { ImGui_ImplOSX_HandleEvent(event, self); }
-(void)mouseUp:(NSEvent *)event             { ImGui_ImplOSX_HandleEvent(event, self); }
-(void)rightMouseUp:(NSEvent *)event        { ImGui_ImplOSX_HandleEvent(event, self); }
-(void)otherMouseUp:(NSEvent *)event        { ImGui_ImplOSX_HandleEvent(event, self); }
-(void)mouseMoved:(NSEvent *)event          { ImGui_ImplOSX_HandleEvent(event, self); }
-(void)rightMouseMoved:(NSEvent *)event     { ImGui_ImplOSX_HandleEvent(event, self); }
-(void)otherMouseMoved:(NSEvent *)event     { ImGui_ImplOSX_HandleEvent(event, self); }
-(void)mouseDragged:(NSEvent *)event        { ImGui_ImplOSX_HandleEvent(event, self); }
-(void)rightMouseDragged:(NSEvent *)event   { ImGui_ImplOSX_HandleEvent(event, self); }
-(void)otherMouseDragged:(NSEvent *)event   { ImGui_ImplOSX_HandleEvent(event, self); }
-(void)scrollWheel:(NSEvent *)event         { ImGui_ImplOSX_HandleEvent(event, self); }

@end

SlideviewerView* g_view;

//-----------------------------------------------------------------------------------
// ImGuiExampleAppDelegate
//-----------------------------------------------------------------------------------

@interface ImGuiExampleAppDelegate : NSObject <NSApplicationDelegate>
@property (nonatomic, readonly) NSWindow* window;
@end

@implementation ImGuiExampleAppDelegate
@synthesize window = _window;

-(BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)theApplication
{
    return YES;
}

-(NSWindow*)window
{
    if (_window != nil)
        return (_window);

    NSRect viewRect = NSMakeRect(100.0, 100.0, 100.0 + 1280.0, 100 + 720.0);

    _window = [[NSWindow alloc] initWithContentRect:viewRect styleMask:NSWindowStyleMaskTitled|NSWindowStyleMaskMiniaturizable|NSWindowStyleMaskResizable|NSWindowStyleMaskClosable backing:NSBackingStoreBuffered defer:YES];
    [_window setTitle:@"Slideviewer"];
    [_window setAcceptsMouseMovedEvents:YES];
    [_window setOpaque:YES];
    [_window makeKeyAndOrderFront:NSApp];

    return (_window);
}

-(void)setupMenu
{
	NSMenu* mainMenuBar = [[NSMenu alloc] init];
    NSMenu* appMenu;
    NSMenuItem* menuItem;

    appMenu = [[NSMenu alloc] initWithTitle:@"Slideviewer"];
    menuItem = [appMenu addItemWithTitle:@"Quit Slideviewer" action:@selector(terminate:) keyEquivalent:@"q"];
    [menuItem setKeyEquivalentModifierMask:NSEventModifierFlagCommand];

    menuItem = [[NSMenuItem alloc] init];
    [menuItem setSubmenu:appMenu];

    [mainMenuBar addItem:menuItem];

    appMenu = nil;
    [NSApp setMainMenu:mainMenuBar];
}

-(void)dealloc
{
    _window = nil;
    [super dealloc];
}

-(void)applicationDidFinishLaunching:(NSNotification *)aNotification
{
	is_program_running = true;

	// Make the application a foreground application (else it won't receive keyboard events)
	ProcessSerialNumber psn = {0, kCurrentProcess};
	TransformProcessType(&psn, kProcessTransformToForegroundApplication);

	// Menu
    [self setupMenu];

	// No multisampling
	uint32_t samples = 0;

	// Keep multisampling attributes at the start of the attribute lists since code below assumes they are array elements 0 through 4.
	NSOpenGLPixelFormatAttribute attrs[] =
	{
		NSOpenGLPFAMultisample,
		NSOpenGLPFASampleBuffers, samples ? 1u : 0u,
		NSOpenGLPFASamples, samples,
		NSOpenGLPFAAccelerated,
		NSOpenGLPFADoubleBuffer,
		NSOpenGLPFAColorSize, 32,
		NSOpenGLPFADepthSize, 24,
		NSOpenGLPFAAlphaSize, 8,
		NSOpenGLPFAOpenGLProfile, NSOpenGLProfileVersion3_2Core,
		0
	};

	NSOpenGLPixelFormat* format = [[NSOpenGLPixelFormat alloc] initWithAttributes:attrs];
	if (!format) {
		bool valid = false;
		while (!format && samples > 0) {
			samples /= 2;
			attrs[2] = samples ? 1 : 0;
			attrs[4] = samples;
			format = [[NSOpenGLPixelFormat alloc] initWithAttributes:attrs];
			if (format) {
				valid = true;
				break;
			}
		}

		if (!valid) {
			NSLog(@"OpenGL pixel format not supported.");
			return;
		}
	}

    SlideviewerView* view = [[SlideviewerView alloc] initWithFrame:self.window.frame pixelFormat:format];
	g_view = view;
    format = nil;
#if MAC_OS_X_VERSION_MAX_ALLOWED >= 1070
    if (floor(NSAppKitVersionNumber) > NSAppKitVersionNumber10_6)
        [view setWantsBestResolutionOpenGLSurface:YES];
#endif // MAC_OS_X_VERSION_MAX_ALLOWED >= 1070
    [self.window setContentView:view];

    if ([view openGLContext] == nil)
        NSLog(@"No OpenGL Context!");

//    init_app_state(&global_app_state, view);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsClassic();

    // Setup Platform/Renderer bindings
    ImGui_ImplOSX_Init();
    ImGui_ImplOpenGL3_Init();

    // Load Fonts
    // - If no fonts are loaded, dear imgui will use the default font. You can also load multiple fonts and use ImGui::PushFont()/PopFont() to select them.
    // - AddFontFromFileTTF() will return the ImFont* so you can store it if you need to select the font among multiple.
    // - If the file cannot be loaded, the function will return NULL. Please handle those errors in your application (e.g. use an assertion, or display an error and quit).
    // - The fonts will be rasterized at a given size (w/ oversampling) and stored into a texture when calling ImFontAtlas::Build()/GetTexDataAsXXXX(), which ImGui_ImplXXXX_NewFrame below will call.
    // - Read 'docs/FONTS.txt' for more instructions and details.
    // - Remember that in C/C++ if you want to include a backslash \ in a string literal you need to write a double backslash \\ !
    //io.Fonts->AddFontDefault();
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf", 16.0f);
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf", 15.0f);
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf", 16.0f);
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/ProggyTiny.ttf", 10.0f);
    ImFont* font = io.Fonts->AddFontFromFileTTF("/System/Library/Fonts/SFNSText.ttf", 32.0f, NULL, io.Fonts->GetGlyphRangesJapanese());
    IM_ASSERT(font != NULL);

    font->Scale = 0.5f;

	unsigned int flags = ImGuiFreeType::LightHinting;
	ImGuiFreeType::BuildFontAtlas(io.Fonts, flags);
}


@end

i64 get_clock() {
	fprintf(stderr, "unimplemented: get_clock()\n");
	return 0; // stub
}

float get_seconds_elapsed(i64 start, i64 end) {
	fprintf(stderr, "unimplemented: get_seconds_elapsed()\n");
	return 0; // stub
}

void message_box(const char* message) {
	fprintf(stderr, "[message box] %s\n", message);
	fprintf(stderr, "unimplemented: message_box()\n");
}

void set_swap_interval(int interval) {
	[g_view setSwapInterval:interval];
}

void mouse_show() {
	fprintf(stderr, "unimplemented: mouse_show()\n");
}

void mouse_hide() {
	fprintf(stderr, "unimplemented: mouse_hide()\n");
}

void open_file_dialog(window_handle_t window) {
	fprintf(stderr, "unimplemented: open_file_dialog()\n");
}

void toggle_fullscreen(window_handle_t window) {
	fprintf(stderr, "unimplemented: toggle_fullscreen()\n");
}

bool check_fullscreen(window_handle_t window) {
	fprintf(stderr, "unimplemented: check_fullscreen()\n");
	return false; // stub
}

bool add_work_queue_entry(work_queue_t* queue, work_queue_callback_t callback, void* userdata) {
	fprintf(stderr, "unimplemented: add_work_queue_entry()\n");
	return false;
}

bool is_queue_work_in_progress(work_queue_t* queue) {
	fprintf(stderr, "unimplemented: is_queue_work_in_progress()\n");
	return false;
}

work_queue_entry_t get_next_work_queue_entry(work_queue_t* queue) {
	work_queue_entry_t result = {};
	fprintf(stderr, "unimplemented: get_next_work_queue_entry()\n");
	return result;
}

void mark_queue_entry_completed(work_queue_t* queue) {
	fprintf(stderr, "unimplemented: mark_queue_entry_completed()\n");
}

bool do_worker_work(work_queue_t* queue, int logical_thread_index) {
	fprintf(stderr, "unimplemented: do_worker_work()\n");
	return false;
}

void get_cpu_info() {
	size_t physical_cpu_count_len = sizeof(physical_cpu_count);
	size_t logical_cpu_count_len = sizeof(logical_cpu_count);
	sysctlbyname("hw.physicalcpu", &physical_cpu_count, &physical_cpu_count_len, NULL, 0);
	sysctlbyname("hw.logicalcpu", &logical_cpu_count, &logical_cpu_count_len, NULL, 0);
	fprintf(stderr,"There are %d physical, %d logical cpu cores\n", physical_cpu_count, logical_cpu_count);
}

int main(int argc, const char* argv[])
{
	g_argc = argc;
	g_argv = argv;
	fprintf(stderr, "Starting up...\n");
	get_cpu_info();


	@autoreleasepool
	{
		NSApp = [NSApplication sharedApplication];
		ImGuiExampleAppDelegate* delegate = [[ImGuiExampleAppDelegate alloc] init];
		[[NSApplication sharedApplication] setDelegate:delegate];
		[NSApp run];
	}
	return NSApplicationMain(argc, argv);
}
