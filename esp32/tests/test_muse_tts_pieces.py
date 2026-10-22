"""Where a streaming reply is cut for Azure's speech (muse_chat_session.cpp):
after its last finished sentence while it comes in, all the rest once it's done."""
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class TtsPieces(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.tmp.cleanup)
        out = Path(cls.tmp.name)
        source = (ROOT / 'components/muse/muse_chat_session.cpp').read_text(encoding='utf-8')
        constants = source[source.index('#define MIC_RATE'):source.index('/* ---- Voice task')]
        types = source[source.index('enum phase_t'):source.index('/* 10 KB')]
        pieces = source[source.index('/* Whether byte k of t ends a sentence'):source.index('/* SSML for n bytes')]
        code = r'''
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "host_compat.h"
#include "cJSON.h"
#include "minimp3.h"
#include "muse_chat_priv.h"
''' + constants + types + r'''
static turn_t s_turn;
static char texts[TEXT_MAX];
''' + pieces + r'''
/* argv: spoken done; stdin: the text so far. Prints the piece's length in bytes. */
int main(int argc, char **argv) {
    (void)argc;
    size_t n = fread(texts, 1, sizeof(texts) - 1, stdin);
    texts[n] = 0;
    s_turn.texts = texts;
    s_turn.nmsgs = 1;
    s_turn.msgs[0].spoken = (size_t)atoi(argv[1]);
    s_turn.msgs[0].done = atoi(argv[2]) != 0;
    printf("%zu", tts_piece(0));
    return 0;
}
'''
        (out / 'pieces.cpp').write_text(code, encoding='utf-8')
        cjson = ROOT / 'managed_components/espressif__cjson/cJSON'
        cls.binary = out / 'pieces'
        proc = subprocess.run(
            [os.environ.get('CXX', 'c++'), '-std=gnu++17', '-Wall', '-Wextra', '-Werror',
             '-include', str(ROOT / 'tests/host_compat.h'), '-I', str(cjson), '-I', str(ROOT / 'tests'),
             '-I', str(ROOT / 'components/muse'), '-I', str(ROOT / 'components/minimp3/include'),
             str(out / 'pieces.cpp'), '-o', str(cls.binary)],
            capture_output=True, text=True)
        if proc.returncode:
            raise AssertionError(proc.stderr)

    def piece(self, text, spoken=0, done=False):
        data, spoken = text.encode(), len(text[:spoken].encode())
        proc = subprocess.run([str(self.binary), str(spoken), '1' if done else '0'],
                              input=data, capture_output=True, check=True)
        return data[spoken:spoken + int(proc.stdout)].decode()

    def test_up_to_the_last_finished_sentence(self):
        self.assertEqual(self.piece('Chào bạn. Hôm nay'), 'Chào bạn.')
        self.assertEqual(self.piece('Một. Hai! Ba? Bốn'), 'Một. Hai! Ba?')
        self.assertEqual(self.piece('Dòng một\nDòng'), 'Dòng một\n')
        self.assertEqual(self.piece('Ừm… để xem'), 'Ừm…')

    def test_a_sentence_ends_only_before_a_space(self):
        self.assertEqual(self.piece('Nhiệt độ 30.5 độ'), '')
        self.assertEqual(self.piece('Chào bạn.'), '')   # more may follow the dot

    def test_from_what_was_said(self):
        self.assertEqual(self.piece('Một. Hai. Ba', spoken=4), ' Hai.')
        self.assertEqual(self.piece('Một. Hai', spoken=4), '')

    def test_all_the_rest_once_done(self):
        self.assertEqual(self.piece('Một. Hai không dấu chấm', spoken=4, done=True), ' Hai không dấu chấm')
        self.assertEqual(self.piece('Một.', spoken=4, done=True), '')


if __name__ == '__main__':
    unittest.main()
