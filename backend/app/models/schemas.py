from pydantic import BaseModel, EmailStr, Field
from datetime import datetime
from typing import Optional, Literal, List


class Token(BaseModel):
    access_token: str
    token_type: str


class UserCreate(BaseModel):
    first_name: str
    last_name: str
    email: EmailStr
    password: str


class GoogleAuthRequest(BaseModel):
    credential: str


class UserResponse(BaseModel):
    id: int
    first_name: str
    last_name: str
    email: str
    role: str = "user"

    class Config:
        from_attributes = True


class PackCreate(BaseModel):
    name: str
    pack_identifier: str
    pairing_code: str = ""
    series_count: int = 3
    parallel_count: int = 1


class PackClaim(BaseModel):
    pairing_code: str


class PackResponse(BaseModel):
    id: int
    name: str
    pack_identifier: str
    pairing_code: str
    master_pairing_code: Optional[str] = None
    master_firmware_version: Optional[str] = None
    series_count: int
    parallel_count: int
    user_id: Optional[int] = None
    auto_created: bool = False

    class Config:
        from_attributes = True


class FirmwareImageResponse(BaseModel):
    id: int
    version: str
    sha256: str
    size: int
    created_at: datetime

    class Config:
        from_attributes = True


class FirmwareDispatchRequest(BaseModel):
    firmware_image_id: int


class BmsCommandRequest(BaseModel):
    action: str
    args: dict = Field(default_factory=dict)


class GroupCreate(BaseModel):
    name: str
    connection_type: Literal["parallel", "series"] = "parallel"
    pack_ids: List[int] = Field(default_factory=list)


class GroupUpdate(BaseModel):
    name: Optional[str] = None
    connection_type: Optional[Literal["parallel", "series"]] = None


class GroupAddPack(BaseModel):
    pack_id: int


class GroupResponse(BaseModel):
    id: int
    name: str
    connection_type: str
    pack_ids: List[int]

    class Config:
        from_attributes = True