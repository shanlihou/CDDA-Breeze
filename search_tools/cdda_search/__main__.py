"""cdda_search 包入口: 支持 `python -m cdda_search`."""

from .cli import main
import sys

sys.exit(main())