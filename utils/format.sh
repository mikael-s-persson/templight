for i in c cc cpp h hpp; do
  for file in $(find -type f -name "*.$i"); do
    clang-format -i $file
  done
done
