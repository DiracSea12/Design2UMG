"""Prove separable extrema keep the existing Pillow 3x3 filter pixel-exact."""
import random
import time
from PIL import Image, ImageFilter
from design2umg_auto_verify import apply_stable_visual_tolerance, build_diff
from unittest.mock import patch


def original(image):
    return image.filter(ImageFilter.MaxFilter(3)).filter(ImageFilter.MinFilter(3))


def main():
    rng = random.Random(57321)
    for size in [(1, 1), (1, 31), (31, 1), (2, 2), (17, 93), (300, 200)]:
        for data in [rng.randbytes(size[0] * size[1] * 4), bytes(size[0] * size[1] * 4),
                     bytes([255]) * (size[0] * size[1] * 4)]:
            image = Image.frombytes('RGBA', size, data)
            expected, actual = original(image), apply_stable_visual_tolerance(image)
            assert expected.tobytes() == actual.tobytes(), size
            assert image.tobytes() == data, 'Input mutated'
    image = Image.frombytes('RGBA', (1920, 1080), rng.randbytes(1920 * 1080 * 4))
    timings = {}
    for name, function in [('rank_filter_reference', original),
                           ('implementation', apply_stable_visual_tolerance)]:
        start = time.perf_counter()
        function(image)
        timings[name] = round(time.perf_counter() - start, 6)
    print('Stable filter: exact equivalence passed;', timings)
    with patch('design2umg_auto_verify.apply_stable_visual_tolerance',
               side_effect=AssertionError('Identical normalized images need no spatial filtering')):
        result = build_diff(image, image.copy(), 6)
    assert result['mean_abs_error'] == result['stable_mean_abs_error'] == 0
    assert result['changed_ratio'] == result['stable_changed_ratio'] == 0
    assert result['diff_bbox'] is result['stable_diff_bbox'] is None


if __name__ == '__main__':
    main()
