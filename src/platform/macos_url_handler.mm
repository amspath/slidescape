/*
  Slidescape, a whole-slide image viewer for digital pathology.
  Copyright (C) 2019-2026  Pieter Valkema

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#import <Cocoa/Cocoa.h>
#include <stdlib.h>
#include <string.h>

static NSMutableArray* macos_pending_urls;
static id macos_url_handler;

@interface SlidescapeURLHandler : NSObject
- (void)handleGetURLEvent:(NSAppleEventDescriptor*)event withReplyEvent:(NSAppleEventDescriptor*)replyEvent;
@end

@implementation SlidescapeURLHandler
- (void)handleGetURLEvent:(NSAppleEventDescriptor*)event withReplyEvent:(NSAppleEventDescriptor*)replyEvent {
	(void)replyEvent;
	NSString* url = [[event paramDescriptorForKeyword:keyDirectObject] stringValue];
	if (url) {
		@synchronized([SlidescapeURLHandler class]) {
			if (!macos_pending_urls) {
				macos_pending_urls = [[NSMutableArray alloc] init];
			}
			[macos_pending_urls addObject:url];
		}
	}
}
@end

extern "C" void macos_register_url_handler(void) {
	@synchronized([SlidescapeURLHandler class]) {
		if (!macos_url_handler) {
			macos_url_handler = [[SlidescapeURLHandler alloc] init];
			if (!macos_pending_urls) {
				macos_pending_urls = [[NSMutableArray alloc] init];
			}
			[[NSAppleEventManager sharedAppleEventManager] setEventHandler:macos_url_handler
			                                                   andSelector:@selector(handleGetURLEvent:withReplyEvent:)
			                                                 forEventClass:kInternetEventClass
			                                                    andEventID:kAEGetURL];
		}
	}
}

extern "C" char* macos_copy_next_open_url(void) {
	char* result = NULL;
	@synchronized([SlidescapeURLHandler class]) {
		if ([macos_pending_urls count] > 0) {
			NSString* url = [macos_pending_urls objectAtIndex:0];
			const char* utf8 = [url UTF8String];
			if (utf8) {
				size_t len = strlen(utf8);
				result = (char*)malloc(len + 1);
				if (result) {
					memcpy(result, utf8, len + 1);
				}
			}
			[macos_pending_urls removeObjectAtIndex:0];
		}
	}
	return result;
}
