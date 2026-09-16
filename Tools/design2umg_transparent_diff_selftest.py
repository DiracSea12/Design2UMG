"""Check transparent normalization against the original pixel-wise definition."""
import random
import time
from PIL import Image
from design2umg_auto_verify import normalize_fully_transparent_rgb


def reference_normalize(left, right):
    left, right = left.copy(), right.copy()
    a, b = left.load(), right.load()
    for y in range(left.height):
        for x in range(left.width):
            if a[x, y][3] == 0 and b[x, y][3] == 0:
                a[x, y] = b[x, y] = (0, 0, 0, 0)
    return left, right


def main():
    rng = random.Random(29481)
    for size in [(1, 1), (2, 256), (127, 93)]:
        images = []
        for _ in range(2):
            pixels = [(rng.randrange(256), rng.randrange(256), rng.randrange(256),
                       rng.choice([0, 0, 1, 127, 254, 255]))
                      for _ in range(size[0] * size[1])]
            image = Image.new('RGBA', size)
            image.putdata(pixels)
            images.append(image)
        originals = [image.tobytes() for image in images]
        expected = reference_normalize(*images)
        actual = normalize_fully_transparent_rgb(*images)
        assert [image.tobytes() for image in actual] == [image.tobytes() for image in expected]
        assert originals == [image.tobytes() for image in images], 'Input mutated'
    # Every pair of alpha values, including one-sided transparency.
    a = Image.new('RGBA', (256, 256))
    b = Image.new('RGBA', a.size)
    a.putdata([(93, 171, 29, x) for y in range(256) for x in range(256)])
    b.putdata([(11, 47, 213, y) for y in range(256) for x in range(256)])
    assert [im.tobytes() for im in normalize_fully_transparent_rgb(a, b)] == [
        im.tobytes() for im in reference_normalize(a, b)]
    a = a.resize((1920, 1080))
    b = b.resize(a.size)
    timings = {}
    for name, function in [('pixel_reference', reference_normalize),
                           ('implementation', normalize_fully_transparent_rgb)]:
        start = time.perf_counter()
        for _ in range(3):
            function(a, b)
        timings[name] = round((time.perf_counter() - start) / 3, 6)
    print('Transparent normalization: exact equivalence passed;', timings)


if __name__ == '__main__':
    main()
