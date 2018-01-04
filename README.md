# desparse
Removes NTFS sparse attribute.

Some applications won't work with sparse files, even if the sparse file is fully filled but has the sparse attribute left.

## Usage
```
C:\> desparse
usage: desparse [-rsh] files ...
opts:
  -r    recursively desparse on directories
  -s    desparse all alternate streams
  -h    this message
  --    stop option parsing
```

## OS Support
Windows 7 and later

## License
Public domain