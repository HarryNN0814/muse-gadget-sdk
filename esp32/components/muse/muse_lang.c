/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "muse_lang.h"

#include <stddef.h>
#include <string.h>

#include "lvgl.h"
#include "muse_text.h"
#include "sdkconfig.h"

/*
 * A translation is looked up by its English text, so a caption the code
 * changes falls back to English rather than to the wrong words.
 * test_muse_lang.py checks that each English text is still in the code, that
 * a translation keeps its format conversions and status names their upper
 * case, and that every letter is one its fonts have. A language adds its
 * tables, the Montserrat sizes its letters need and a Kconfig choice.
 */
typedef struct {
    const char *en, *text;
} muse_lang_entry_t;

#if CONFIG_MUSE_UI_LANG_VI
/* Short names, upper case like the English: they head the screen. */
static const muse_lang_entry_t STATUS[] = {
    { "WAKING UP", "ĐANG KHỞI ĐỘNG" },
    { "READY", "SẴN SÀNG" },
    { "LISTENING", "ĐANG NGHE" },
    { "THINKING", "ĐANG NGHĨ" },
    { "SPEAKING", "ĐANG NÓI" },
    { "ERROR", "LỖI" },
    { "GOODBYE", "TẠM BIỆT" },
    { "WI-FI OFF", "WI-FI ĐANG TẮT" },
    { "SET UP WI-FI", "CHƯA CÀI WI-FI" },
    { "NO WI-FI", "KHÔNG CÓ WI-FI" },
    { "RECONNECTING", "ĐANG KẾT NỐI LẠI" },
    { "CONNECTING", "ĐANG KẾT NỐI" },
    { "USB POWER", "NGUỒN USB" },
    { "CHARGING %d%%", "ĐANG SẠC %d%%" },
    { "BATTERY %d%%", "PIN %d%%" },
};

/* Sentence case: a capital with two marks (Ấ, Ễ) is cramped in 16 px. */
static const muse_lang_entry_t MESSAGES[] = {
    /* The voice path */
    { "WAKING UP...", "Đang khởi động..." },
    { "LISTENING...", "Đang nghe..." },
    { "RECORDING...", "Đang ghi âm..." },
    { "LISTENING %.1fs", "Đang nghe %.1fs" },
    { "RECORDING %.1fs", "Đang ghi âm %.1fs" },
    { "SENDING VOICE NOTE", "Đang gửi tin nhắn thoại" },
    { "NOTE SENT - WAITING FOR MUSE", "Đã gửi, đang chờ Muse" },
    { "MUSE HAS AN ANSWER", "Muse đã có kết quả" },
    { "TALK, OR TAP TO STOP", "Nói đi, hoặc chạm để dừng" },
    { "Muse is working on it. I'll tell you when it's done.", "Muse đang xử lý. Xong tôi sẽ báo bạn." },
    { "HOLD LONGER TO TALK", "Giữ nút lâu hơn để nói" },
    { "SET UP MUSE FIRST", "Hãy cài đặt Muse trước" },
    { "NO WI-FI", "Không có Wi-Fi" },
    { "AUDIO INIT FAILED", "Lỗi khởi động âm thanh" },
    /* Notes saved to send later */
    { "COULDN'T SAVE THE NOTE", "Không lưu được tin nhắn" },
    { "SAVED, WILL TRY AGAIN", "Đã lưu, sẽ thử lại" },
    { "SAVED, SENDS WHEN ONLINE", "Đã lưu, sẽ gửi khi có mạng" },
    { "SENDING SAVED NOTE", "Đang gửi tin nhắn đã lưu" },
    { "SAVED NOTE SENT", "Đã gửi tin nhắn đã lưu" },
    { "COULDN'T SEND A SAVED NOTE", "Không gửi được tin nhắn đã lưu" },
    { "SAVED NOTE: WILL TRY AGAIN", "Tin nhắn đã lưu: sẽ thử lại" },
    { "NOTES STILL WAITING TO SEND", "Còn tin nhắn chờ gửi" },
    /* A turn that failed */
    { "CAN'T REACH MUSE", "Không kết nối được Muse" },
    { "CAN'T REACH AZURE", "Không kết nối được Azure" },
    { "MUSE NOT SET UP", "Chưa cài đặt Muse" },
    { "LOST CONNECTION TO MUSE", "Mất kết nối với Muse" },
    { "NO REPLY FROM MUSE", "Muse chưa trả lời" },
    { "DIDN'T CATCH THAT", "Chưa nghe rõ, nói lại nhé" },
    { "MUSE STOPPED LISTENING", "Muse đã ngừng nghe" },
    { "MUSE COULDN'T LISTEN", "Muse không nghe được" },
    { "MUSE DIDN'T TAKE IT", "Muse không nhận tin nhắn" },
    { "INTERRUPTED", "Đã bị ngắt" },
    { "CANCELLED", "Đã huỷ" },
    { "SETTINGS CHANGED", "Đã đổi cài đặt" },
    { "REPLY TOO LONG", "Câu trả lời quá dài" },
    { "REPLY TOO LONG: SEE MUSE APP", "Câu trả lời quá dài: xem trong app Muse" },
    { "REPLY BUFFER LIMIT - TRY AGAIN", "Bộ nhớ đệm đầy, thử lại nhé" },
    { "MUSE REPLY ACCESS DENIED (403)", "Muse từ chối truy cập (403)" },
    { "MUSE REPLY AUTH REQUIRED (401)", "Muse cần đăng nhập lại (401)" },
    { "CAN'T SUBSCRIBE TO MUSE", "Không nhận được tin từ Muse" },
    { "CAN'T KEEP UP", "Xử lý không kịp" },
    { "OUT OF MEMORY", "Hết bộ nhớ" },
    /* Buttons and power */
    { "SPEAKER ON", "Đã bật loa" },
    { "SPEAKER OFF", "Đã tắt loa" },
    { "HOLD TO MUTE", "Giữ để tắt tiếng" },
    { "HOLD TO UNMUTE", "Giữ để bật tiếng" },
    { "HOLD TO POWER OFF", "Giữ để tắt máy" },
    { "GOODBYE!", "Tạm biệt!" },
    { "COULDN'T POWER OFF", "Không tắt được máy" },
    { "PHONE SETUP ON", "Đã bật cài đặt qua điện thoại" },
    { "PHONE SETUP: %s", "Cài đặt qua điện thoại: %s" },
    { "PHONE SETUP OFF", "Đã tắt cài đặt qua điện thoại" },
    { "RESETTING...", "Đang đặt lại..." },
};

/* The button menu (muse_menu.c), the pairing card (muse_ui.c) and the touch
 * boards' settings pages (muse_settings_ui.c): sentence case, in Montserrat,
 * but for the titles and pages, drawn in unscii, whose accented letters come
 * from the caption font. */
static const muse_lang_entry_t MENU[] = {
    /* Rows */
    { "Volume", "Âm lượng" },
    { "Speaker", "Loa" },
    { "Brightness", "Độ sáng" },
    { "Mic gain", "Độ nhạy mic" },
    { "Auto-sleep", "Tự ngủ" },
    { "Phone setup", "Cài qua điện thoại" },
    { "Status", "Trạng thái" },
    { "Battery", "Pin" },
    { "Reset pairing", "Đặt lại ghép nối" },
    { "Screen off", "Tắt màn hình" },
    { "Power off", "Tắt máy" },
    { "Close menu", "Đóng menu" },
    /* What the talk button does */
    { "Change", "Đổi" },
    { "Toggle", "Bật/tắt" },
    { "Open", "Mở" },
    { "Select", "Chọn" },
    { "Close", "Đóng" },
    { "Back", "Quay lại" },
    { "Reset", "Đặt lại" },
    { "Cancel", "Huỷ" },
    { "Esc Back", "Esc Quay lại" },
    { "Esc Cancel", "Esc Huỷ" },
    { LV_SYMBOL_DOWN " Down", LV_SYMBOL_DOWN " Xuống" },
    { "Down " LV_SYMBOL_RIGHT, "Xuống " LV_SYMBOL_RIGHT },
    /* Values */
    { "On", "Bật" },
    { "Off", "Tắt" },
    { "Not set", "Chưa cài" },
    { "Joining", "Đang vào" },
    { "Failed", "Lỗi" },
    { "Not found", "Không thấy" },
    { "Never", "Không bao giờ" },
    { "30 s", "30 giây" },
    { "1 min", "1 phút" },
    { "2 min", "2 phút" },
    { "5 min", "5 phút" },
    { "10 min", "10 phút" },
    { "Custom", "Tuỳ chỉnh" },
    /* Titles, upper case */
    { "MENU", "CÀI ĐẶT" },
    { "MENU  ^v Move  <> Change", "CÀI ĐẶT  ^v Chọn  <> Đổi" },
    { "STATUS", "TRẠNG THÁI" },
    { "BATTERY", "PIN" },
    { "POWER OFF", "TẮT MÁY" },
    { "RESET PAIRING", "ĐẶT LẠI GHÉP NỐI" },
    /* Pages, their columns lined up as in the English */
    { "Wi-Fi %s\nIP    %s\nLink  %s\nMuse  %s\nPhone %s\nPower %s\nVer   %s",
      "Wi-Fi %s\nIP    %s\nLink  %s\nMuse  %s\nĐT    %s\nNguồn %s\nBản   %s" },
    { "off", "tắt" },
    { "offline", "mất mạng" },
    { "Connected", "Đã kết nối" },
    { "Starting", "Đang khởi động" },
    { "Ready to pair", "Chờ ghép nối" },
    { "App connected", "App đã kết nối" },
    { "Confirm pairing", "Xác nhận ghép nối" },
    { "Connecting", "Đang kết nối" },
    { "Online", "Trực tuyến" },
    { "Offline", "Ngoại tuyến" },
    { "Error", "Lỗi" },
    { "Not set up", "Chưa cài" },
    { "Saved", "Đã lưu" },
    { "Can't connect", "Không kết nối được" },
    { "Unplug USB to\nmeasure how\nlong the\nbattery lasts.", "Rút USB để\nđo xem pin\ndùng được\nbao lâu." },
    { "%s %s\nBatt  %d>%d%%\nRate  %s\nFull  %s\nOff   %s\nSleep %s\nWakes %s\nBusy  %s",
      "%s %s\nPin   %d>%d%%\nHao   %s\nĐầy   %s\nTắt   %s\nNgủ   %s\nThức  %s\nBận   %s" },
    { "On batt", "Dùng pin" },
    { "Last run", "Lần trước" },
    { "Turn Muse off?\n\nPress the %s button to turn it back on.", "Tắt Muse?\n\nBấm nút %s để bật lại." },
    { "Forget Wi-Fi and the Muse app pairing, then restart?", "Quên Wi-Fi và ghép nối app Muse, rồi khởi động lại?" },
    /* The pairing card */
    { "Pairing code", "Mã ghép nối" },
    { "Enter on phone", "Nhập trên điện thoại" },
    { "Enter it on your phone", "Nhập mã trên điện thoại" },
    { "Tap screen", "Chạm màn hình" },
    { "Tap to confirm pairing", "Chạm để xác nhận ghép nối" },
    { "Press", "Bấm" },
    { "Press button", "Bấm nút" },
    { "%s button", "Nút %s" },
    { "Press the %s button", "Bấm nút %s" },
    { "Muse app", "App Muse" },
    { "Pair with Muse app", "Ghép nối với app Muse" },
    /* The touch boards' settings pages (muse_settings_ui.c) */
    { "SETTINGS", "CÀI ĐẶT" },
    { "SOUND", "ÂM THANH" },
    { "AUTO-SLEEP", "TỰ NGỦ" },
    { "POWER", "NGUỒN" },
    { "Sound", "Âm thanh" },
    { "Sleep", "Tự ngủ" },
    { "Phone camera", "Camera điện thoại" },
    { "Not nearby", "Không ở gần" },
    { "Mic only", "Chỉ có mic" },
    { "No audio", "Không có tiếng" },
    { "Vol %d%%", "Âm %d%%" },
    { "Muted", "Đã tắt tiếng" },
    { "Ready", "Sẵn sàng" },
    { "No phone", "Chưa có ĐT" },
    { "Show", "Hiện" },
    { "Hide", "Ẩn" },
    { "Scan for networks", "Quét mạng" },
    { "Scanning...", "Đang quét..." },
    { "Other network...", "Mạng khác..." },
    { "Other network", "Mạng khác" },
    { "Network name", "Tên mạng" },
    { "Password", "Mật khẩu" },
    { "Empty if it's open", "Để trống nếu mạng mở" },
    { "MAC address", "Địa chỉ MAC" },
    { "Saved networks", "Mạng đã lưu" },
    { "Tap to forget", "Chạm để quên" },
    { "Hidden", "Ẩn" },
    { "saved", "đã lưu" },
    { "secured", "có khoá" },
    { "open", "mở" },
    { "No networks found", "Không tìm thấy mạng" },
    { "Muse remembers up to 8 networks and joins the strongest one in range. Tap a saved one twice to forget it.", "Muse nhớ tối đa 8 mạng và tự vào mạng mạnh nhất trong tầm sóng. Chạm hai lần vào mạng đã lưu để quên." },
    { "Wi-Fi is off", "Wi-Fi đang tắt" },
    { "No saved networks. Scan and pick one.", "Chưa lưu mạng nào. Hãy quét và chọn một mạng." },
    { "Joining %s\n%s", "Đang vào %s\n%s" },
    { "Connected to %s\n%s", "Đã kết nối tới %s\n%s" },
    { "Connected to %s\n%s  -  %d dBm", "Đã kết nối tới %s\n%s  -  %d dBm" },
    { "No saved network nearby\nLooking again within a minute", "Không có mạng đã lưu ở gần\nSẽ tìm lại trong vòng một phút" },
    { "Couldn't join %s\n%s", "Không vào được %s\n%s" },
    { "Server", "Máy chủ" },
    { "Muse server", "Máy chủ Muse" },
    { "Device token", "Token thiết bị" },
    { "SDK token", "Token SDK" },
    { "Test connection", "Kiểm tra kết nối" },
    { "Tap again to reset", "Chạm lần nữa để đặt lại" },
    { "Resetting...", "Đang đặt lại..." },
    { "Empty keeps the current one; a new one restarts", "Để trống để giữ token hiện tại; token mới sẽ khởi động lại" },
    { "Empty keeps the current one", "Để trống để giữ token hiện tại" },
    { "Empty for the default", "Để trống để dùng mặc định" },
    { "Optional", "Tuỳ chọn" },
    { "Pair with the Muse app to use your account; a device token here overrides it, and a long one is easier to send over Bluetooth. The VM ID picks one of your VMs. The SDK token is from gadgets.muse.ai; one set here replaces the firmware's. Reset pairing forgets Wi-Fi and the app pairing, then restarts.", "Ghép nối với app Muse để dùng tài khoản của bạn; token thiết bị nhập ở đây sẽ thay thế, và token dài thì gửi qua Bluetooth dễ hơn. VM ID chọn một trong các VM của bạn. Token SDK lấy từ gadgets.muse.ai; token nhập ở đây thay cho token trong firmware. Đặt lại ghép nối sẽ quên Wi-Fi và ghép nối với app, rồi khởi động lại." },
    { "not paired", "chưa ghép nối" },
    { "paired", "đã ghép nối" },
    { "paired with another SDK token, pair again to use this one", "đã ghép với token SDK khác, hãy ghép lại để dùng token này" },
    { "Muse app: %s\n%s", "App Muse: %s\n%s" },
    { "Set here", "Nhập ở đây" },
    { "Set", "Đã cài" },
    { "Not a token", "Không phải token" },
    { "Forget paired phones", "Quên điện thoại đã ghép" },
    { "When on, Muse is visible to phones nearby. Open tools/ble_setup.html in Chrome, connect, and enter the code Muse shows to pair.", "Khi bật, điện thoại ở gần sẽ thấy Muse. Mở tools/ble_setup.html trong Chrome, kết nối rồi nhập mã Muse hiển thị để ghép nối." },
    { "Visible as %s", "Hiển thị là %s" },
    { "Phone connected\n%s", "Điện thoại đã kết nối\n%s" },
    { "Paired", "Đã ghép nối" },
    { "Waiting for pairing", "Đang chờ ghép nối" },
    { "Mic level", "Mức mic" },
    { "Talk at arm's length: the bar should reach green (-30 to -15 dBFS) without going orange.", "Nói cách một sải tay: thanh phải lên tới màu xanh lá (-30 đến -15 dBFS) mà không chuyển sang cam." },
    { "30 seconds", "30 giây" },
    { "1 minute", "1 phút" },
    { "2 minutes", "2 phút" },
    { "5 minutes", "5 phút" },
    { "10 minutes", "10 phút" },
    { "Turn the screen off after Muse has been idle for:", "Tắt màn hình sau khi Muse không hoạt động trong:" },
    { "Sleep now", "Ngủ ngay" },
    { "Tap the screen or press either button to wake.", "Chạm màn hình hoặc bấm một trong hai nút để đánh thức." },
    { "Used", "Đã dùng" },
    { "A full charge", "Một lần sạc đầy" },
    { "Chip asleep", "Chip ngủ" },
    { "Wakes", "Lần thức" },
    { "CPU busy", "CPU bận" },
    { "Start over", "Đo lại" },
    { "Measures from unplugging USB until it's plugged back in. The gauge moves in 1% steps, so give it a few hours. Chip asleep is time in light sleep; CPU busy is time a core was running a task.", "Đo từ lúc rút USB đến khi cắm lại. Đồng hồ pin chỉ nhảy 1% sau mỗi lần thay đổi, nên cần vài giờ. Chip ngủ là thời gian ngủ nhẹ; CPU bận là thời gian một lõi đang chạy tác vụ." },
    { "%d h %d min", "%d giờ %d phút" },
    { "%d min", "%d phút" },
    { "No battery", "Không có pin" },
    { "Unplug USB to start measuring.", "Rút USB để bắt đầu đo." },
    { "On battery for %s", "Đã chạy bằng pin %s" },
    { "Last run: %s on battery", "Lần trước: %s chạy bằng pin" },
    { "None", "Không có" },
    { "lasts ~%d h", "đủ ~%d giờ" },
    { "%d%% so far", "%d%% đến giờ" },
    { "measuring", "đang đo" },
    { "Also kept awake by: %s", "Còn bị giữ thức bởi: %s" },
    { "Power Muse off completely?", "Tắt hẳn Muse?" },
    { "Press the %s button to turn it back on. To just turn the screen off, press the %s button.", "Bấm nút %s để bật lại. Muốn chỉ tắt màn hình, bấm nút %s." },
    { "Press the %s button to turn it back on.", "Bấm nút %s để bật lại." },
};

static const char *lookup(const muse_lang_entry_t *t, size_t n, const char *en)
{
    for (size_t i = 0; i < n; i++) {
        if (!strcmp(t[i].en, en)) {
            return t[i].text;
        }
    }
    return en;
}

const char *muse_lang_status(const char *en)
{
    return lookup(STATUS, sizeof(STATUS) / sizeof(STATUS[0]), en);
}

const char *muse_lang_message(const char *en)
{
    return lookup(MESSAGES, sizeof(MESSAGES) / sizeof(MESSAGES[0]), en);
}

const char *muse_lang_menu(const char *en)
{
    return lookup(MENU, sizeof(MENU) / sizeof(MENU[0]), en);
}

/* LVGL's Montserrat has ASCII and its symbols; fonts/muse_font_vi_<px>.c has
 * Montserrat's Vietnamese letters at the same size and baseline. */
#if LV_FONT_MONTSERRAT_12
LV_FONT_DECLARE(muse_font_vi_12)
#endif
LV_FONT_DECLARE(muse_font_vi_14)
LV_FONT_DECLARE(muse_font_vi_16)
LV_FONT_DECLARE(muse_font_vi_20)
LV_FONT_DECLARE(muse_font_vi_28)

const lv_font_t *muse_lang_montserrat(const lv_font_t *font)
{
    static const struct {
        const lv_font_t *base, *letters;
    } SIZES[] = {
#if LV_FONT_MONTSERRAT_12
        { &lv_font_montserrat_12, &muse_font_vi_12 },
#endif
        { &lv_font_montserrat_14, &muse_font_vi_14 },
        { &lv_font_montserrat_16, &muse_font_vi_16 },
        { &lv_font_montserrat_20, &muse_font_vi_20 },
        { &lv_font_montserrat_28, &muse_font_vi_28 },
    };
    static lv_font_t copies[sizeof(SIZES) / sizeof(SIZES[0])];
    for (size_t i = 0; i < sizeof(SIZES) / sizeof(SIZES[0]); i++) {
        if (font == SIZES[i].base) {
            if (!copies[i].get_glyph_dsc) {
                copies[i] = *font;
                copies[i].fallback = SIZES[i].letters;
            }
            return &copies[i];
        }
    }
    return font;
}
#else
const char *muse_lang_status(const char *en)
{
    return en;
}

const char *muse_lang_message(const char *en)
{
    return en;
}

const char *muse_lang_menu(const char *en)
{
    return en;
}

const lv_font_t *muse_lang_montserrat(const lv_font_t *font)
{
    return font;
}
#endif

#if CONFIG_MUSE_LATIN_FONT
LV_FONT_DECLARE(muse_font_latin_16)
#endif

const lv_font_t *muse_lang_label_font(const char *text, const lv_font_t *plain)
{
#if CONFIG_MUSE_LATIN_FONT
    if (muse_text_has_latin(text)) {
        return &muse_font_latin_16;
    }
#endif
    (void)text;
    return plain;
}
