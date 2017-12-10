#include <pebble.h>

static struct {

	Window *window;
	Layer *root_layer;
	
	TextLayer *time_layer;
	TextLayer *date_layer;
	TextLayer *steps_layer;
	
	char time_str[16];
	char date_str[16];
	char steps_str[16];
	
	GFont date_font;
	GColor date_color;
	
	GFont time_font;
	
	GColor steps_color;
	GFont steps_font;

} app;

static void _create_date_layer() {
	app.date_layer = text_layer_create(GRect(0, 0, 1, 1));
	text_layer_set_background_color(app.date_layer, GColorClear);
	text_layer_set_text_alignment(app.date_layer, GTextAlignmentCenter);
	text_layer_set_font(app.date_layer, app.date_font);
	text_layer_set_text_color(app.date_layer, app.date_color);
	layer_add_child(app.root_layer, text_layer_get_layer(app.date_layer));
}

static void _create_time_layer() {
	
	app.time_layer = text_layer_create(GRect(0, 0, 1, 1));
	
	text_layer_set_text_alignment(app.time_layer, GTextAlignmentCenter);
	text_layer_set_font(app.time_layer, app.time_font);
	
	text_layer_set_background_color(app.time_layer, GColorClear);
	text_layer_set_text_color(app.time_layer, GColorWhite);
	
	layer_add_child(app.root_layer, text_layer_get_layer(app.time_layer));		
}

static void _create_steps_layer() {
	app.steps_layer = text_layer_create(GRect(0, 0, 1, 1));
	text_layer_set_background_color(app.steps_layer, GColorClear);
	text_layer_set_text_alignment(app.steps_layer, GTextAlignmentCenter);
	text_layer_set_font(app.steps_layer, app.steps_font);
	text_layer_set_text_color(app.steps_layer, app.steps_color);
	layer_add_child(app.root_layer, text_layer_get_layer(app.steps_layer));
}

#define GOLDEN 0.618

static void _layout_layers() {
		
	GRect bounds = layer_get_bounds(app.root_layer);	
	
	GSize timeSize = text_layer_get_content_size(app.time_layer);	
	// Adding a bit more to compensate for pixels in the top of the layer for this particular font.
	const int16_t timeHeightCompensation = 13;
	timeSize.h += timeHeightCompensation;
	GRect timeFrame = GRect(
		bounds.origin.x, 
		bounds.origin.y + (1 - GOLDEN) * (bounds.size.h - timeSize.h),  
		bounds.size.w, timeSize.h
	);
	layer_set_frame(text_layer_get_layer(app.time_layer), timeFrame);
	
	const int16_t dateTimePadding = 0;

	GSize dateSize = text_layer_get_content_size(app.date_layer);
	GRect dateFrame = GRect(
		bounds.origin.x,
		timeFrame.origin.y - dateSize.h - dateTimePadding,
		bounds.size.w,
		dateSize.h
	);
	layer_set_frame(text_layer_get_layer(app.date_layer), dateFrame);
	
	GSize stepsSize = text_layer_get_content_size(app.steps_layer);
	const int16_t stepsHeightCompensation = 9;
	stepsSize.h += stepsHeightCompensation;
	int16_t timeBottom = (timeFrame.origin.y + timeFrame.size.h - timeHeightCompensation);
	int16_t bottom = bounds.origin.y + bounds.size.h;
	GRect stepsFrame = GRect(
		bounds.origin.x,
		timeBottom + (bottom - timeBottom - stepsSize.h) / 2,
		bounds.size.w,
		stepsSize.h
	);
	layer_set_frame(text_layer_get_layer(app.steps_layer), stepsFrame);
}

static char _toupper(char ch) {
	if ('a' <= ch && ch <= 'z')
		return ch + ('A' - 'a');
	else
		return ch;
}

static void _update_time_date() {
	
	time_t temp = time(NULL);
	struct tm *tick_time = localtime(&temp);

	strftime(app.time_str, sizeof(app.time_str), "%H:%M", tick_time);
	text_layer_set_text(app.time_layer, app.time_str);
	
	strftime(app.date_str, sizeof(app.date_str), "%d %b", tick_time);
	for (char *ch = app.date_str; *ch; ch++)
		*ch = _toupper(*ch);
	text_layer_set_text(app.date_layer, app.date_str);
}

typedef enum {
	StepsStateNormal,
	StepsStateBelowAverage,
	StepsStateAboveAverage
} StepsState;

static const uint32_t StepsStateKey = 0x0001;

static void _steps_state_did_change(StepsState new_state) {
	if (new_state == StepsStateBelowAverage) {
		vibes_long_pulse();
	}
}

static void _update_steps_state(StepsState new_state) {
	StepsState current = persist_read_int(StepsStateKey);
	if (new_state != current) {
		persist_write_int(StepsStateKey, new_state);
		_steps_state_did_change(new_state);
	}
}

static void _update_steps() {
		
	//
	// Getting the total and the average number of steps.
	//
	time_t start = time_start_of_today();
	time_t end = time(NULL);
	
	HealthValue average;
	HealthServiceAccessibilityMask averageMask = health_service_metric_averaged_accessible(
		HealthMetricStepCount, 
		start, end, 
		HealthServiceTimeScopeDailyWeekdayOrWeekend
	);
	if (averageMask & HealthServiceAccessibilityMaskAvailable) {
		average = health_service_sum_averaged(
			HealthMetricStepCount, start, end, HealthServiceTimeScopeDailyWeekdayOrWeekend
		);
	} else {
		average = -1;
	}
	
	HealthValue steps_count;
	if (health_service_metric_accessible(HealthMetricStepCount, start, end) & HealthServiceAccessibilityMaskAvailable) {
		steps_count = health_service_sum_today(HealthMetricStepCount);
	} else {
		steps_count = -1;
	}
	
	//
	// Updating the label accordingly.
	//
	if (steps_count >= 0) {
		snprintf(app.steps_str, sizeof(app.steps_str), "%dK", (int)(steps_count / 1000));
	} else {
		strcpy(app.steps_str, "?");
	}
	text_layer_set_text(app.steps_layer, app.steps_str);

	// And its color.
	GColor color = app.steps_color;
	const float difference_threshold = 0.1;
	const int16_t min_steps_to_colorize = 1000;
	if (steps_count >= 0 && average > 0 && (steps_count >= min_steps_to_colorize || average > min_steps_to_colorize)) {
		float difference = (steps_count - average) / (float)average;
		if (difference > difference_threshold) {
			color = GColorYellow;
			_update_steps_state(StepsStateAboveAverage);
		} else if (difference < -difference_threshold) {
			color = GColorRed;
			_update_steps_state(StepsStateBelowAverage);
		} else {
			_update_steps_state(StepsStateNormal);
		}
	}
	text_layer_set_text_color(app.steps_layer, color);
}

static void _tick_handler(struct tm *tick_time, TimeUnits units_changed) {
	_update_time_date();
	_update_steps();
	_layout_layers();
}

static void _health_handler(HealthEventType event, void *context) {
	
	switch (event) {
		
		case HealthEventSignificantUpdate:
			APP_LOG(APP_LOG_LEVEL_DEBUG, "HealthEventSignificantUpdate");
			_update_steps();
			_layout_layers();
			break;
			
		case HealthEventMovementUpdate:
			APP_LOG(APP_LOG_LEVEL_DEBUG,  "HealthEventMovementUpdate");
			_update_steps();
			_layout_layers();
			break;
			
		default:
			break;
	}
}

static void _window_load(Window *window) {
	
	window_set_background_color(app.window, GColorBlack);
		
	app.root_layer = window_get_root_layer(app.window);
	
	_create_time_layer();
	_create_date_layer();
	_create_steps_layer();
	
	_update_time_date();
	_update_steps();
	_layout_layers();
	
	tick_timer_service_subscribe(MINUTE_UNIT, _tick_handler);
	
	if (!health_service_events_subscribe(_health_handler, NULL)) {
		APP_LOG(APP_LOG_LEVEL_ERROR, "Cannot subscribe to the health service");
	}
}

static void _window_unload(Window *window) {
	
	text_layer_destroy(app.time_layer);
	text_layer_destroy(app.date_layer);
	
	health_service_events_unsubscribe();
	tick_timer_service_unsubscribe();
}

static void init(void) {
	
	app.date_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_LECO_20));
	app.time_font = fonts_get_system_font(FONT_KEY_LECO_42_NUMBERS);
	app.steps_font = fonts_load_custom_font(resource_get_handle(RESOURCE_ID_LECO_30));
	app.date_color = GColorCyan;
	app.steps_color = GColorCyan;
	
	app.window = window_create();
	window_set_window_handlers(app.window, (WindowHandlers) {
		.load = _window_load,
		.unload = _window_unload,
	});
	window_stack_push(app.window, true);
}

static void deinit(void) {
	window_destroy(app.window);
}

int main(void) {
	
	init();
	
	APP_LOG(APP_LOG_LEVEL_DEBUG, "Done initializing");
	
	app_event_loop();
	
	deinit();
}
