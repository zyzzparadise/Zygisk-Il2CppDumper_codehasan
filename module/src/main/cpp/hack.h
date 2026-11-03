//
// Created by Perfare on 2020/7/4.
//

#ifndef ZYGISK_IL2CPPDUMPER_HACK_H
#define ZYGISK_IL2CPPDUMPER_HACK_H

#include <cstddef>
#include <jni.h>

void hack_prepare(
        const char *game_data_dir,
        void *data,
        size_t length,
        JavaVM *vm);

#endif //ZYGISK_IL2CPPDUMPER_HACK_H
