/* OLE DB Internal header
 *
 * Copyright 2009 Huw Davies
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

HRESULT create_oledb_convert(IUnknown *outer, void **obj) DECLSPEC_HIDDEN;
HRESULT create_data_init(IUnknown *outer, void **obj) DECLSPEC_HIDDEN;
HRESULT create_error_info(IUnknown *outer, void **obj) DECLSPEC_HIDDEN;
HRESULT create_oledb_rowpos(IUnknown *outer, void **obj) DECLSPEC_HIDDEN;
