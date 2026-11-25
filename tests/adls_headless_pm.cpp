#include "kern_xelp_donor.hpp"
#include <cassert>
#include <fstream>
#include <iterator>
#include <vector>

int main(int argc, char **argv) {
    using namespace XELPDonor::HeadlessPM;
    std::vector<uint8_t> image(Site + sizeof(Before));
    if (argc == 2) {
        std::ifstream donor(argv[1], std::ios::binary);
        assert(donor);
        image.assign(std::istreambuf_iterator<char>(donor), std::istreambuf_iterator<char>());
    } else {
        memcpy(image.data() + Site, Before, sizeof(Before));
    }

    assert(matches(image.data(), image.size()));
    assert(!matches(image.data(), Site + sizeof(Before) - 1));
    for (size_t offset = Site; offset < Site + sizeof(Before); ++offset) {
        image[offset] ^= 1;
        assert(!matches(image.data(), image.size()));
        image[offset] ^= 1;
    }
    assert(Branch + 2 + image[Branch + 1] == Site + sizeof(Before));
    memcpy(image.data() + Branch, FallThrough, sizeof(FallThrough));
    assert(image[Branch] == 0x90 && image[Branch + 1] == 0x90);
    constexpr size_t afterBranch = Branch - Site + sizeof(FallThrough);
    assert(!memcmp(image.data() + Site + afterBranch, Before + afterBranch,
                   sizeof(Before) - afterBranch));
}
