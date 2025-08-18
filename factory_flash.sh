#!/bin/bash
echo "Write Factory Data"

# 파라미터 체크
if [ $# -eq 0 ]; then
    echo "Usage: $0 <1|2|3>"
    exit 1
fi

# case문으로 분기
case "$1" in
    1)
        echo "write case1"
        esptool.py write_flash 0x3E0000 out/fff2_8001/4a67147f-9eb7-411f-ba43-b67951be8e1b/4a67147f-9eb7-411f-ba43-b67951be8e1b-partition.bin
        ;;
    2)
        echo "write case2"
        esptool.py write_flash 0x3E0000 out/fff2_8001/34c9fdf9-0e49-4c2f-bf00-bdefe93f3d9f/34c9fdf9-0e49-4c2f-bf00-bdefe93f3d9f-partition.bin
        ;;
    3)
        echo "write case3"
        esptool.py write_flash 0x3E0000 out/fff2_8001/55f6b997-d9eb-4b5c-9976-109806ac1229/55f6b997-d9eb-4b5c-9976-109806ac1229-partition.bin
        ;;
    *)
        echo "Error: Invalid parameter '$1'"
        echo "Available options: 1, 2, 3"
        exit 1
        ;;
esac    

echo "Flash write completed!"