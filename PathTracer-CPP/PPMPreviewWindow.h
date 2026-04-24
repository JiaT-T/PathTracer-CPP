#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <iomanip>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

class PPMPreviewWindow
{
public:
	PPMPreviewWindow(const std::string& title, int width, int height)
		: state(std::make_shared<SharedState>())
	{
		state->width = width;
		state->height = height;
		state->title = widen(title);
		state->dib_pixels.assign(static_cast<size_t>(width) * height * 4, 0);
		state->info_text = build_rendering_text(0.0, 0, width, height);

		ui_thread = std::thread(&PPMPreviewWindow::ui_thread_main, state);
	}

	~PPMPreviewWindow()
	{
		close_window();
		if (ui_thread.joinable())
			ui_thread.join();
	}

	void UpdateImage(const std::vector<unsigned char>& dib_pixels, int completed_rows, double elapsed_seconds)
	{
		HWND hwnd = nullptr;
		{
			std::lock_guard<std::mutex> lock(state->mutex);
			if (state->closed)
				return;

			state->dib_pixels = dib_pixels;
			state->info_text = build_rendering_text(elapsed_seconds, completed_rows, state->width, state->height);
			hwnd = state->hwnd;
		}

		if (hwnd)
			PostMessageW(hwnd, WM_APP + 1, 0, 0);
	}

	void SetFinished(double render_seconds)
	{
		HWND hwnd = nullptr;
		{
			std::lock_guard<std::mutex> lock(state->mutex);
			if (state->closed)
				return;

			state->info_text = build_finished_text(render_seconds);
			hwnd = state->hwnd;
		}

		if (hwnd)
			PostMessageW(hwnd, WM_APP + 1, 0, 0);
	}

	void WaitUntilClosed()
	{
		std::unique_lock<std::mutex> lock(state->mutex);
		state->cv.wait(lock, [this] { return state->closed; });
	}

	bool IsClosed() const
	{
		std::lock_guard<std::mutex> lock(state->mutex);
		return state->closed;
	}

private:
	struct SharedState
	{
		int width = 0;
		int height = 0;
		std::wstring title;
		std::wstring info_text;
		std::vector<unsigned char> dib_pixels;
		HWND hwnd = nullptr;
		bool closed = false;
		std::mutex mutex;
		std::condition_variable cv;
	};

	struct WindowContext
	{
		std::shared_ptr<SharedState> state;
		BITMAPINFO bitmap_info{};
		double zoom_factor = 1.0;
		double pan_x = 0.0;
		double pan_y = 0.0;
		bool dragging = false;
		POINT last_mouse{};
	};

	static constexpr int kHeaderHeight = 56;
	static constexpr int kOuterMargin = 16;
	static constexpr UINT kRefreshMessage = WM_APP + 1;

	std::shared_ptr<SharedState> state;
	std::thread ui_thread;

	static std::wstring widen(const std::string& text)
	{
		if (text.empty())
			return L"PPM Preview";

		int required_size = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
		if (required_size <= 0)
		{
			required_size = MultiByteToWideChar(CP_ACP, 0, text.c_str(), -1, nullptr, 0);
			if (required_size <= 0)
				return std::wstring(text.begin(), text.end());

			std::wstring wide(required_size - 1, L'\0');
			MultiByteToWideChar(CP_ACP, 0, text.c_str(), -1, wide.data(), required_size);
			return wide;
		}

		std::wstring wide(required_size - 1, L'\0');
		MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wide.data(), required_size);
		return wide;
	}

	std::wstring build_finished_text(double render_seconds) const
	{
		std::wostringstream stream;
		stream << std::fixed << std::setprecision(2)
			   << L"Render Time: " << render_seconds << L" s"
			   << L"    Resolution: " << state->width << L" x " << state->height;
		return stream.str();
	}

	static std::wstring build_rendering_text(double elapsed_seconds, int completed_rows, int width, int height)
	{
		std::wostringstream stream;
		stream << std::fixed << std::setprecision(2)
			   << L"Elapsed: " << elapsed_seconds << L" s"
			   << L"    Resolution: " << width << L" x " << height;

		stream.seekp(0, std::ios_base::end);
		stream << L"    Scanlines: " << completed_rows << L" / " << height;
		return stream.str();
	}

	void close_window()
	{
		HWND hwnd = nullptr;
		{
			std::lock_guard<std::mutex> lock(state->mutex);
			hwnd = state->hwnd;
		}

		if (hwnd)
			PostMessageW(hwnd, WM_CLOSE, 0, 0);
	}

	static void ui_thread_main(std::shared_ptr<SharedState> state)
	{
		const wchar_t* class_name = L"PathTracerLivePPMPreviewWindow";
		HINSTANCE instance = GetModuleHandleW(nullptr);

		WNDCLASSW window_class{};
		window_class.lpfnWndProc = &PPMPreviewWindow::window_proc;
		window_class.hInstance = instance;
		window_class.lpszClassName = class_name;
		window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
		window_class.hbrBackground = nullptr;

		if (!RegisterClassW(&window_class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
		{
			std::lock_guard<std::mutex> lock(state->mutex);
			state->closed = true;
			state->cv.notify_all();
			return;
		}

		WindowContext context{};
		context.state = state;
		context.bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		context.bitmap_info.bmiHeader.biWidth = state->width;
		context.bitmap_info.bmiHeader.biHeight = -state->height;
		context.bitmap_info.bmiHeader.biPlanes = 1;
		context.bitmap_info.bmiHeader.biBitCount = 32;
		context.bitmap_info.bmiHeader.biCompression = BI_RGB;

		const int screen_width = GetSystemMetrics(SM_CXSCREEN);
		const int screen_height = GetSystemMetrics(SM_CYSCREEN);
		const int desired_client_width = std::min(std::max(state->width + 2 * kOuterMargin, 720), screen_width * 9 / 10);
		const int desired_client_height = std::min(std::max(state->height + kHeaderHeight + 2 * kOuterMargin, 540), screen_height * 9 / 10);

		RECT window_rect{ 0, 0, desired_client_width, desired_client_height };
		AdjustWindowRect(&window_rect, WS_OVERLAPPEDWINDOW, FALSE);

		HWND hwnd = CreateWindowExW(
			0,
			class_name,
			state->title.c_str(),
			WS_OVERLAPPEDWINDOW | WS_VISIBLE,
			CW_USEDEFAULT,
			CW_USEDEFAULT,
			window_rect.right - window_rect.left,
			window_rect.bottom - window_rect.top,
			nullptr,
			nullptr,
			instance,
			&context);

		if (!hwnd)
		{
			std::lock_guard<std::mutex> lock(state->mutex);
			state->closed = true;
			state->cv.notify_all();
			return;
		}

		ShowWindow(hwnd, SW_SHOW);
		UpdateWindow(hwnd);

		MSG msg{};
		while (GetMessageW(&msg, nullptr, 0, 0) > 0)
		{
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}
	}

	static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param)
	{
		WindowContext* context = nullptr;

		if (message == WM_NCCREATE)
		{
			auto* create_struct = reinterpret_cast<CREATESTRUCTW*>(l_param);
			context = static_cast<WindowContext*>(create_struct->lpCreateParams);
			SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(context));

			std::lock_guard<std::mutex> lock(context->state->mutex);
			context->state->hwnd = hwnd;
		}
		else
		{
			context = reinterpret_cast<WindowContext*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
		}

		if (!context)
			return DefWindowProcW(hwnd, message, w_param, l_param);

		return handle_message(hwnd, *context, message, w_param, l_param);
	}

	static LRESULT handle_message(HWND hwnd, WindowContext& context, UINT message, WPARAM w_param, LPARAM l_param)
	{
		switch (message)
		{
		case WM_PAINT:
			paint(hwnd, context);
			return 0;
		case WM_ERASEBKGND:
			return 1;
		case kRefreshMessage:
			InvalidateRect(hwnd, nullptr, FALSE);
			return 0;
		case WM_MOUSEWHEEL:
			handle_mouse_wheel(hwnd, context, GET_WHEEL_DELTA_WPARAM(w_param), GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param));
			return 0;
		case WM_LBUTTONDOWN:
			context.dragging = true;
			context.last_mouse = POINT{ GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param) };
			SetCapture(hwnd);
			return 0;
		case WM_MOUSEMOVE:
			if (context.dragging)
			{
				POINT current{ GET_X_LPARAM(l_param), GET_Y_LPARAM(l_param) };
				context.pan_x += current.x - context.last_mouse.x;
				context.pan_y += current.y - context.last_mouse.y;
				context.last_mouse = current;
				InvalidateRect(hwnd, nullptr, FALSE);
			}
			return 0;
		case WM_LBUTTONUP:
			context.dragging = false;
			if (GetCapture() == hwnd)
				ReleaseCapture();
			return 0;
		case WM_RBUTTONDBLCLK:
			context.zoom_factor = 1.0;
			context.pan_x = 0.0;
			context.pan_y = 0.0;
			InvalidateRect(hwnd, nullptr, FALSE);
			return 0;
		case WM_DESTROY:
		{
			std::lock_guard<std::mutex> lock(context.state->mutex);
			context.state->hwnd = nullptr;
			context.state->closed = true;
			context.state->cv.notify_all();
			PostQuitMessage(0);
			return 0;
		}
		default:
			return DefWindowProcW(hwnd, message, w_param, l_param);
		}
	}

	static void handle_mouse_wheel(HWND hwnd, WindowContext& context, short delta, int screen_x, int screen_y)
	{
		POINT cursor{ screen_x, screen_y };
		ScreenToClient(hwnd, &cursor);

		RECT client_rect{};
		GetClientRect(hwnd, &client_rect);

		const int client_width = static_cast<int>(client_rect.right - client_rect.left);
		const int client_height = static_cast<int>(client_rect.bottom - client_rect.top);
		const int available_width = std::max(1, client_width - 2 * kOuterMargin);
		const int available_height = std::max(1, client_height - kHeaderHeight - kOuterMargin);

		const double fit_scale_x = static_cast<double>(available_width) / context.state->width;
		const double fit_scale_y = static_cast<double>(available_height) / context.state->height;
		const double fit_scale = std::max(0.01, std::min(fit_scale_x, fit_scale_y));
		const double old_scale = fit_scale * context.zoom_factor;

		const double zoom_step = delta > 0 ? 1.1 : 1.0 / 1.1;
		context.zoom_factor = std::clamp(context.zoom_factor * zoom_step, 0.1, 20.0);
		const double new_scale = fit_scale * context.zoom_factor;

		const double base_draw_width = context.state->width * old_scale;
		const double base_draw_height = context.state->height * old_scale;
		const double base_x = (client_width - base_draw_width) / 2.0 + context.pan_x;
		const double image_area_top = kHeaderHeight + kOuterMargin / 2.0;
		const double base_y = image_area_top + std::max(0.0, (available_height - base_draw_height) / 2.0) + context.pan_y;

		const double image_x = (cursor.x - base_x) / old_scale;
		const double image_y = (cursor.y - base_y) / old_scale;

		const double new_base_draw_width = context.state->width * new_scale;
		const double new_base_draw_height = context.state->height * new_scale;
		const double centered_x = (client_width - new_base_draw_width) / 2.0;
		const double centered_y = image_area_top + std::max(0.0, (available_height - new_base_draw_height) / 2.0);

		context.pan_x = cursor.x - centered_x - image_x * new_scale;
		context.pan_y = cursor.y - centered_y - image_y * new_scale;

		InvalidateRect(hwnd, nullptr, FALSE);
	}

	static void paint(HWND hwnd, WindowContext& context)
	{
		PAINTSTRUCT paint_struct{};
		HDC hdc = BeginPaint(hwnd, &paint_struct);

		RECT client_rect{};
		GetClientRect(hwnd, &client_rect);

		const int client_width = static_cast<int>(client_rect.right - client_rect.left);
		const int client_height = static_cast<int>(client_rect.bottom - client_rect.top);

		HDC memory_dc = CreateCompatibleDC(hdc);
		HBITMAP back_buffer = CreateCompatibleBitmap(hdc, std::max(1, client_width), std::max(1, client_height));
		HGDIOBJ old_bitmap = SelectObject(memory_dc, back_buffer);

		HBRUSH background_brush = CreateSolidBrush(RGB(248, 248, 248));
		FillRect(memory_dc, &client_rect, background_brush);
		DeleteObject(background_brush);

		std::wstring info_text;
		{
			std::lock_guard<std::mutex> lock(context.state->mutex);
			info_text = context.state->info_text;
		}

		RECT header_rect{ kOuterMargin, kOuterMargin / 2, client_rect.right - kOuterMargin, kHeaderHeight };
		SetBkMode(memory_dc, TRANSPARENT);
		SetTextColor(memory_dc, RGB(32, 32, 32));
		DrawTextW(memory_dc, info_text.c_str(), -1, &header_rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

		const int available_width = std::max(1, client_width - 2 * kOuterMargin);
		const int available_height = std::max(1, client_height - kHeaderHeight - kOuterMargin);

		const double fit_scale_x = static_cast<double>(available_width) / context.state->width;
		const double fit_scale_y = static_cast<double>(available_height) / context.state->height;
		const double scale = std::max(0.01, std::min(fit_scale_x, fit_scale_y) * context.zoom_factor);

		const int draw_width = std::max(1, static_cast<int>(std::round(context.state->width * scale)));
		const int draw_height = std::max(1, static_cast<int>(std::round(context.state->height * scale)));
		const int image_area_top = kHeaderHeight + kOuterMargin / 2;
		const int centered_x = (client_width - draw_width) / 2;
		const int centered_y = image_area_top + std::max(0, (available_height - draw_height) / 2);
		const int draw_x = centered_x + static_cast<int>(std::round(context.pan_x));
		const int draw_y = centered_y + static_cast<int>(std::round(context.pan_y));

		RECT image_border{ draw_x - 1, draw_y - 1, draw_x + draw_width + 1, draw_y + draw_height + 1 };
		FrameRect(memory_dc, &image_border, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));

		SetStretchBltMode(memory_dc, HALFTONE);
		{
			std::lock_guard<std::mutex> lock(context.state->mutex);
			StretchDIBits(
				memory_dc,
				draw_x,
				draw_y,
				draw_width,
				draw_height,
				0,
				0,
				context.state->width,
				context.state->height,
				context.state->dib_pixels.data(),
				&context.bitmap_info,
				DIB_RGB_COLORS,
				SRCCOPY);
		}

		BitBlt(hdc, 0, 0, client_width, client_height, memory_dc, 0, 0, SRCCOPY);
		SelectObject(memory_dc, old_bitmap);
		DeleteObject(back_buffer);
		DeleteDC(memory_dc);

		EndPaint(hwnd, &paint_struct);
	}
};
