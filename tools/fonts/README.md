# 字体源文件（生成用，不随固件烧录）

`tools/gen_fonts.py` 和 `tools/gen_icons.py` 需要一份 TTF/OTF 才能在**生成时**
栅格化字形；栅格化后的位图已经是 `include/fonts/*.h` 里的 C 数组，正常编译固件
不需要这个文件。

本仓库使用 **Noto Sans SC**（Google，SIL Open Font License 1.1），它同时覆盖
拉丁字母、数字和简体中文，所以标题、正文、大号温度数字和中文标签都来自同一套
字体，风格统一：

```sh
cd tools/fonts
curl -L -o NotoSansSC-VF.ttf \
  "https://cdn.jsdelivr.net/gh/google/fonts@main/ofl/notosanssc/NotoSansSC%5Bwght%5D.ttf"
```

（约 18 MB。*.ttf 已被 .gitignore 忽略，因为它只是生成期素材。）

然后重新生成所有位图资源：

```sh
tools/gen_all.sh
```

## 许可

Noto Sans SC 采用 [SIL Open Font License 1.1](https://openfontlicense.org/)，
允许把栅格化后的位图嵌入固件；仓库里保存的是位图数组，不是字体文件本身。
换字体时请确认目标字体的许可同样允许嵌入位图。
