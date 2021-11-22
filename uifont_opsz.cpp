// Compile with
// c++ -std=c++17 -framework ApplicationServices uifont_opsz.cpp -o uifont_opsz

// On at least macOS Big Sur 11.3 Beta (20E5172i) and probably extending back to the initial release of Big Sur.
// If CTFontCreateCopyWithAttributes is used to make a copy of a system font with an opsz axis and a variation is
// specified but the opsz is not changed, then the variation is applied, but the new font (with a different variation)
// compares equal to the original font.
//
// The behavior seems to change when the initial fonts opsz axis was clamped.
//
// Using CTFontDescriptorCreateCopyWithAttributes and CTFontCreateWithFontDescriptor instead to make the copy
// results in the variation not being set, but the resulting copy correctly compares equal to the original.

#include <ApplicationServices/ApplicationServices.h>

#include <stdio.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <initializer_list>
#include <string>
#include <vector>

CTFontRef make_ctfont_from_file(const char* file, CGFloat size) {
    struct Data { void* addr; size_t length; };

    FILE* fileHandle = fopen(file, "rb");
    if (!fileHandle) {
        printf("Could not open: %s\n", file);
        return nullptr;
    }
    int fileDescriptor = fileno(fileHandle);
    struct stat fileStatus;
    int err = fstat(fileDescriptor, &fileStatus);
    size_t fileSize = static_cast<size_t>(fileStatus.st_size);
    void* fileMmap = mmap(nullptr, fileSize, PROT_READ, MAP_PRIVATE, fileDescriptor, 0);
    fclose(fileHandle);
    Data* info = new Data{fileMmap, fileSize};
    CFAllocatorContext ctx = {
        0, // CFIndex version
        info, // void* info
        nullptr, // const void *(*retain)(const void *info);
        nullptr, // void (*release)(const void *info);
        nullptr, // CFStringRef (*copyDescription)(const void *info);
        nullptr, // void * (*allocate)(CFIndex size, CFOptionFlags hint, void *info);
        nullptr, // void*(*reallocate)(void* ptr,CFIndex newsize,CFOptionFlags hint,void* info);
        [](void*,void* info) -> void { // void (*deallocate)(void *ptr, void *info);
            Data* data = (Data*)info;
            munmap(data->addr, data->length);
            delete data;
        },
        nullptr, // CFIndex (*preferredSize)(CFIndex size, CFOptionFlags hint, void *info);
    };
    CFAllocatorRef alloc = CFAllocatorCreate(kCFAllocatorDefault, &ctx);
    CFDataRef fileData = CFDataCreateWithBytesNoCopy(kCFAllocatorDefault, (const UInt8 *)fileMmap, fileSize, alloc);
    CFRelease(alloc);

    CTFontDescriptorRef desc = CTFontManagerCreateFontDescriptorFromData(fileData);
    CTFontRef ctFont = CTFontCreateWithFontDescriptor(desc, size, nullptr);
    CFRelease(fileData);
    CFRelease(desc);

    return ctFont;
}

CTFontRef make_ctfont_from_uifont(CGFloat size) {
    // kCTFontUIFontSystem, kCTFontUIFontMessage
    return CTFontCreateUIFontForLanguage(kCTFontUIFontSystem, size, nullptr);
}

std::string tag_to_string(uint32_t tag) {
    char buffer[5];
    buffer[0] = (tag & 0xff000000) >> 24;
    buffer[1] = (tag & 0xff0000) >> 16;
    buffer[2] = (tag & 0xff00) >> 8;
    buffer[3] = tag & 0xff;
    buffer[4] = 0;
    return std::string(buffer);
}

uint32_t constexpr make_tag(char a, char b, char c, char d) {
    return (((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)c << 8) | (uint32_t)d);
}

int main() {
  struct TestCase {
      CTFontRef font;
      const char* name;
      ~TestCase() { CFRelease(font); }
  } testCases[] = {
      { make_ctfont_from_uifont(24), "SystemUI size 24" },
      { make_ctfont_from_file("/System/Library/Fonts/SFNS.ttf", 24), "/System/Library/Fonts/SFNS.ttf" },
      //{ make_ctfont_from_file("SFNS#1.ttf", 24), "/System/Library/Fonts/SFNS.ttf" },
 
      { make_ctfont_from_uifont(17.00), "SystemUI size 17.00" },
      { make_ctfont_from_uifont(17.01), "SystemUI size 17.01" },
      { make_ctfont_from_uifont(95.99), "SystemUI size 95.99" },
      { make_ctfont_from_uifont(96.00), "SystemUI size 96.00" },
  };

  constexpr uint32_t kOpszTag = make_tag('o', 'p', 's', 'z');
  constexpr uint32_t kWdthTag = make_tag('w', 'd', 't', 'h');
  constexpr uint32_t kWghtTag = make_tag('w', 'g', 'h', 't');

  for (const TestCase& testCase : testCases) {
      CTFontRef originalFont = testCase.font;
      CTFontDescriptorRef originalDescriptor = CTFontCopyFontDescriptor(originalFont);
      CFArrayRef originalAxes = CTFontCopyVariationAxes(originalFont);
      CFIndex originalAxisCount = CFArrayGetCount(originalAxes);
      CFDictionaryRef originalVariation = CTFontCopyVariation(originalFont);

      CFMutableDictionaryRef originalResolvedVariation = 
              CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
                                        &kCFTypeDictionaryKeyCallBacks,
                                        &kCFTypeDictionaryValueCallBacks);

      printf("--------------------------\n");
      printf("Case: %s\n", testCase.name);
      printf("Original: ");
      for (int i = 0; i < originalAxisCount; ++i) {
          CFDictionaryRef originalAxis = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(originalAxes, i));

          long tagLong;
          CFNumberRef tagNumber = static_cast<CFNumberRef>(CFDictionaryGetValue(originalAxis, kCTFontVariationAxisIdentifierKey));
          CFNumberGetValue(tagNumber, kCFNumberLongType, &tagLong);

          double defDouble;
          CFNumberRef defNumber = static_cast<CFNumberRef>(CFDictionaryGetValue(originalAxis, kCTFontVariationAxisDefaultValueKey));
          CFNumberGetValue(defNumber, kCFNumberDoubleType, &defDouble);

          double valueDouble = defDouble;

          CFNumberRef currentNumber = static_cast<CFNumberRef>(CFDictionaryGetValue(originalVariation, tagNumber));
          if (currentNumber) {
              double currentDouble;
              CFNumberGetValue(currentNumber, kCFNumberDoubleType, &currentDouble);
              valueDouble = currentDouble;
          }

          printf("(%s: %f) ", tag_to_string(tagLong).c_str(), valueDouble);

          CFNumberRef valueNumber = CFNumberCreate(kCFAllocatorDefault, kCFNumberDoubleType, &valueDouble);
          CFDictionaryAddValue(originalResolvedVariation, tagNumber, valueNumber);
          CFRelease(valueNumber);
      }
      printf("\n\n");

      long opszLong = kOpszTag;
      CFNumberRef opszNumber = CFNumberCreate(kCFAllocatorDefault, kCFNumberLongType, &opszLong);

      for (bool omit_opsz : {false, true}) {
      for (uint32_t axis_to_bump : { 0u, kOpszTag, kWdthTag }) {
      for (CGFloat wghtValue : {100, 200, 300, 400, 500, 600, 700, 800, 900}) {
          CFMutableDictionaryRef requestedVariation = 
                  CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
                                            &kCFTypeDictionaryKeyCallBacks,
                                            &kCFTypeDictionaryValueCallBacks);
          printf("Request : ");
          for (int i = 0; i < originalAxisCount; ++i) {
              CFDictionaryRef originalAxis = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(originalAxes, i));

              long tagLong;
              CFNumberRef tagNumber = static_cast<CFNumberRef>(CFDictionaryGetValue(originalAxis, kCTFontVariationAxisIdentifierKey));
              CFNumberGetValue(tagNumber, kCFNumberLongType, &tagLong);

              CFNumberRef currentNumber = static_cast<CFNumberRef>(CFDictionaryGetValue(originalResolvedVariation, tagNumber));
              assert(currentNumber);
              double currentDouble;
              CFNumberGetValue(currentNumber, kCFNumberDoubleType, &currentDouble);
              double valueDouble = currentDouble;
 
              if (tagLong == kOpszTag && omit_opsz) {
                  printf("#%s: %f# ", tag_to_string(tagLong).c_str(), valueDouble);
                  continue;
              }
              if (tagLong == axis_to_bump) {
                  valueDouble += 0.0001f;
              }
              if (tagLong == kWghtTag) {
                  valueDouble = wghtValue;
              }

              printf("(%s: %f) ", tag_to_string(tagLong).c_str(), valueDouble);

              CFNumberRef valueNumber = CFNumberCreate(kCFAllocatorDefault, kCFNumberDoubleType, &valueDouble);
              CFDictionaryAddValue(requestedVariation, tagNumber, valueNumber);
              CFRelease(valueNumber);
          }
          printf("\n");

          CFMutableDictionaryRef requestedAttributes =
                    CFDictionaryCreateMutable(kCFAllocatorDefault, 0,
                                              &kCFTypeDictionaryKeyCallBacks,
                                              &kCFTypeDictionaryValueCallBacks);
          CFDictionaryAddValue(requestedAttributes, kCTFontVariationAttribute, requestedVariation);
#if 0
          CTFontDescriptorRef requestedDescriptor = CTFontDescriptorCreateWithAttributes(requestedAttributes);
          fflush(stdout);
          CFShow(requestedDescriptor);
          CTFontRef resultFont = CTFontCreateCopyWithAttributes(originalFont, 0, nullptr, requestedDescriptor);
          CFRelease(requestedDescriptor);
#else
          // This gives somewhat different results.
          // The variation isn't applied unless opsz changes, but the result makes CFEqual correct.
          CGFloat size = CTFontGetSize(originalFont);
          CTFontDescriptorRef resultDescriptor = CTFontDescriptorCreateCopyWithAttributes(originalDescriptor, requestedAttributes);
          fflush(stdout);
          //CFShow(resultDescriptor);
          CTFontRef resultFont = CTFontCreateWithFontDescriptor(resultDescriptor, size, nullptr);
          CFRelease(resultDescriptor);
#endif

          CFArrayRef resultAxes = CTFontCopyVariationAxes(resultFont);
          CFIndex resultAxisCount = CFArrayGetCount(resultAxes);
          CFDictionaryRef resultVariation = CTFontCopyVariation(resultFont);

          printf("Result  : ");
          for (int i = 0; i < resultAxisCount; ++i) {
              CFDictionaryRef resultAxis = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(resultAxes, i));
    
              long tagLong;
              CFNumberRef tagNumber = static_cast<CFNumberRef>(CFDictionaryGetValue(resultAxis, kCTFontVariationAxisIdentifierKey));
              CFNumberGetValue(tagNumber, kCFNumberLongType, &tagLong);
    
              double defDouble;
              CFNumberRef defNumber = static_cast<CFNumberRef>(CFDictionaryGetValue(resultAxis, kCTFontVariationAxisDefaultValueKey));
              CFNumberGetValue(defNumber, kCFNumberDoubleType, &defDouble);
    
              double valueDouble = defDouble;
    
              CFNumberRef currentNumber = static_cast<CFNumberRef>(CFDictionaryGetValue(resultVariation, tagNumber));
              if (currentNumber) {
                  double currentDouble;
                  CFNumberGetValue(currentNumber, kCFNumberDoubleType, &currentDouble);
                  valueDouble = currentDouble;
              }
    
              printf("(%s: %f) ", tag_to_string(tagLong).c_str(), valueDouble);
          }
          printf("\n");


          CFArrayRef originalAxes = CTFontCopyVariationAxes(testCase.font);
          CFIndex originalAxisCount = CFArrayGetCount(originalAxes);
          CFDictionaryRef originalVariation = CTFontCopyVariation(testCase.font);
    
          printf("Original: ");
          for (int i = 0; i < originalAxisCount; ++i) {
              CFDictionaryRef originalAxis = static_cast<CFDictionaryRef>(CFArrayGetValueAtIndex(originalAxes, i));
    
              long tagLong;
              CFNumberRef tagNumber = static_cast<CFNumberRef>(CFDictionaryGetValue(originalAxis, kCTFontVariationAxisIdentifierKey));
              CFNumberGetValue(tagNumber, kCFNumberLongType, &tagLong);
    
              double defDouble;
              CFNumberRef defNumber = static_cast<CFNumberRef>(CFDictionaryGetValue(originalAxis, kCTFontVariationAxisDefaultValueKey));
              CFNumberGetValue(defNumber, kCFNumberDoubleType, &defDouble);
    
              double valueDouble = defDouble;
    
              CFNumberRef currentNumber = static_cast<CFNumberRef>(CFDictionaryGetValue(originalVariation, tagNumber));
              if (currentNumber) {
                  double currentDouble;
                  CFNumberGetValue(currentNumber, kCFNumberDoubleType, &currentDouble);
                  valueDouble = currentDouble;
              }
    
              printf("(%s: %f) ", tag_to_string(tagLong).c_str(), valueDouble);
          }
          printf("\n");

          bool variationEqual = CFEqual(resultVariation, originalVariation);
          printf("CFEqual(resultVariation, originalVariation): %s\n", variationEqual ? "true" : "false");
          //CFShow(resultVariation);
          //CFShow(originalVariation);

          // This shows the issue.
          // The variation has changed, but if opsz didn't change then it is still equal.
          // If variationEqual is false then fontEqual should also be false.
          bool fontEqual = CFEqual(resultFont, testCase.font);
          printf("CFEqual(resultFont, originalFont): %s\n", fontEqual ? "true" : "false");
          fflush(stdout);
          //CFShow(resultFont);
          //CFShow(originalFont);
          
          printf("\n");

          CFRelease(originalVariation);
          CFRelease(originalAxes);
          CFRelease(resultVariation);
          CFRelease(resultAxes);
          CFRelease(resultFont);
          CFRelease(requestedAttributes);
          CFRelease(requestedVariation);
      }}}
      CFRelease(opszNumber);
      CFRelease(originalResolvedVariation);
      CFRelease(originalVariation);
      CFRelease(originalAxes);
      CFRelease(originalDescriptor);
  }
}
